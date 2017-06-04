#ifdef WIN32
#include <windows.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <chrono>
#include <ctime>
#include <cstring>
#include <hidapi/hidapi.h>

#define JOYCON_L_BT (0x2006)
#define JOYCON_R_BT (0x2007)
#define PRO_CONTROLLER (0x2009)
#define JOYCON_CHARGING_GRIP (0x200e)
unsigned short product_ids[] = {JOYCON_L_BT, JOYCON_R_BT, PRO_CONTROLLER, JOYCON_CHARGING_GRIP};

// Uncomment for spam or SPI dumping
//#define DEBUG_PRINT
//#define DUMP_SPI
//#define REPLAY
//#define WEIRD_VIBRATION_TEST
//#define WRITE_TEST
//#define LED_TEST
#define INPUT_LOOP

bool bluetooth = true;
uint8_t global_count = 0;

void hex_dump(unsigned char *buf, int len)
{
    for (int i = 0; i < len; i++)
        printf("%02x ", buf[i]);
    printf("\n");
}

void hid_exchange(hid_device *handle, unsigned char *buf, int len)
{
    if(!handle) return; //TODO: idk I just don't like this to be honest
    
#ifdef DEBUG_PRINT
    hex_dump(buf, len);
#endif
    
    hid_write(handle, buf, len);

    int res = hid_read(handle, buf, 0x400);
#ifdef DEBUG_PRINT
    if(res > 0)
        hex_dump(buf, res);
#endif
}

int hid_dual_exchange(hid_device *handle_l, hid_device *handle_r, unsigned char *buf_l, unsigned char *buf_r, int len)
{
    int res = -1;
    
    if(handle_l && buf_l)
    {
        hid_set_nonblocking(handle_l, 1);
        res = hid_write(handle_l, buf_l, len);
        res = hid_read(handle_l, buf_l, 0x400);
#ifdef DEBUG_PRINT
        if(res > 0)
            hex_dump(buf_l, res);
#endif
        hid_set_nonblocking(handle_l, 0);
    }
    
    if(handle_r && buf_r)
    {
        hid_set_nonblocking(handle_r, 1);
        res = hid_write(handle_r, buf_r, len);
        //usleep(17000);
        do
        {
            res = hid_read(handle_r, buf_r, 0x400);
        }
        while(!res);
#ifdef DEBUG_PRINT
        if(res > 0)
            hex_dump(buf_r, res);
#endif
        hid_set_nonblocking(handle_r, 0);
    }
    
    return res;
}

void joycon_send_command(hid_device *handle, int command, uint8_t *data, int len)
{
    unsigned char buf[0x400];
    memset(buf, 0, 0x400);
    
    if(!bluetooth)
    {
        buf[0x00] = 0x80;
        buf[0x01] = 0x92;
        buf[0x03] = 0x31;
    }
    
    buf[bluetooth ? 0x0 : 0x8] = command;
    if(data != NULL && len != 0)
        memcpy(buf + (bluetooth ? 0x1 : 0x9), data, len);
    
    hid_exchange(handle, buf, len + (bluetooth ? 0x1 : 0x9));
    
    if(data)
        memcpy(data, buf, 0x40);
}

void joycon_send_subcommand(hid_device *handle, int command, int subcommand, uint8_t *data, int len)
{
    unsigned char buf[0x400];
    memset(buf, 0, 0x400);
    
    uint8_t rumble_base[9] = {(++global_count) & 0xF, 0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40};
    memcpy(buf, rumble_base, 9);
    
    buf[9] = subcommand;
    if(data && len != 0)
        memcpy(buf + 10, data, len);
        
    joycon_send_command(handle, command, buf, 10 + len);
        
    if(data)
        memcpy(data, buf, 0x40); //TODO
}

void spi_flash_dump(hid_device *handle, char *out_path)
{
    unsigned char buf[0x400];
    uint8_t *spi_read = (uint8_t*)calloc(1, 0x26 * sizeof(uint8_t));
    
    FILE *dump = fopen(out_path, "wb");
    if(dump == NULL)
    {
        printf("Failed to open dump file %s, aborting...\n", out_path);
        return;
    }
    
    uint32_t* offset = (uint32_t*)(&spi_read[0x0]);
    for(*offset = 0x0; *offset < 0x80000; *offset += 0x1C)
    {
        // HACK/TODO: hid_exchange loves to return data from the wrong addr, or 0x30 (NACK?) packets
        // so let's make sure our returned data is okay before writing
        while(1)
        {
            memcpy(buf, spi_read, 0x26);
            joycon_send_subcommand(handle, 0x1, 0x10, buf, 0x26);
            
            // sanity-check our data, loop if it's not good
            if((buf[0] == (bluetooth ? 0x21 : 0x81)) && (*(uint32_t*)&buf[0xF + (bluetooth ? 0 : 10)] == *offset))
                break;
        }
        
        fwrite(buf + (0x14 + (bluetooth ? 0 : 10)) * sizeof(char), 0x1C, 1, dump);
        
        if((*offset & 0xFF) == 0) // less spam
            printf("\rDumped 0x%05X of 0x80000", *offset);
    }
    printf("\rDumped 0x80000 of 0x80000\n");
    fclose(dump);
}

void spi_write(hid_device *handle, uint32_t offs, uint8_t *data, uint8_t len)
{
    unsigned char buf[0x400];
    uint8_t *spi_write = (uint8_t*)calloc(1, 0x26 * sizeof(uint8_t));
    uint32_t* offset = (uint32_t*)(&spi_write[0]);
    uint8_t* length = (uint8_t*)(&spi_write[4]);
   
    *length = len;
    *offset = offs;
    memcpy(&spi_write[0x5], data, len);
   
    int max_write_count = 30;
    do
    {
        //usleep(300000);
        memcpy(buf, spi_write, 0x39);
        joycon_send_subcommand(handle, 0x1, 0x11, buf, 0x26);
    }
    while(buf[0x10 + (bluetooth ? 0 : 10)] != 0x11 && buf[0] != (bluetooth ? 0x21 : 0x81));
}

void spi_read(hid_device *handle, uint32_t offs, uint8_t *data, uint8_t len)
{
    unsigned char buf[0x400];
    uint8_t *spi_read = (uint8_t*)calloc(1, 0x26 * sizeof(uint8_t));
    uint32_t* offset = (uint32_t*)(&spi_read[0]);
    uint8_t* length = (uint8_t*)(&spi_read[4]);
   
    *length = len;
    *offset = offs;
   
    int max_read_count = 30;
    do
    {
        //usleep(300000);
        memcpy(buf, spi_read, 0x36);
        joycon_send_subcommand(handle, 0x1, 0x10, buf, 0x26);
    }
    while(*(uint32_t*)&buf[0xF + (bluetooth ? 0 : 10)] != *offset);
    
    memcpy(data, &buf[0x14 + (bluetooth ? 0 : 10)], len);
}

int joycon_init(hid_device *handle, const wchar_t *name)
{
    unsigned char buf[0x400];
    unsigned char sn_buffer[14] = {0x00};
    memset(buf, 0, 0x400);
    
    
    if(!bluetooth)
    {
        // Get MAC Left
        memset(buf, 0x00, 0x400);
        buf[0] = 0x80;
        buf[1] = 0x01;
        hid_exchange(handle, buf, 0x2);
        
        if(buf[2] == 0x3)
        {
            printf("%ls disconnected!\n", name);
            return -1;
        }
        else
        {
            printf("Found %ls, MAC: %02x:%02x:%02x:%02x:%02x:%02x\n", name,
                   buf[9], buf[8], buf[7], buf[6], buf[5], buf[4]);
        }
            
        // Do handshaking
        memset(buf, 0x00, 0x400);
        buf[0] = 0x80;
        buf[1] = 0x02;
        hid_exchange(handle, buf, 0x2);
        
        printf("Switching baudrate...\n");
        
        // Switch baudrate to 3Mbit
        memset(buf, 0x00, 0x400);
        buf[0] = 0x80;
        buf[1] = 0x03;
        hid_exchange(handle, buf, 0x2);
        
        // Do handshaking again at new baudrate so the firmware pulls pin 3 low?
        memset(buf, 0x00, 0x400);
        buf[0] = 0x80;
        buf[1] = 0x02;
        hid_exchange(handle, buf, 0x2);
        
        // Only talk HID from now on
        memset(buf, 0x00, 0x400);
        buf[0] = 0x80;
        buf[1] = 0x04;
        hid_exchange(handle, buf, 0x2);
    }

    // Enable vibration
    memset(buf, 0x00, 0x400);
    buf[0] = 0x01; // Enabled
    joycon_send_subcommand(handle, 0x1, 0x48, buf, 1);
    
    // Enable IMU data
    memset(buf, 0x00, 0x400);
    buf[0] = 0x01; // Enabled
    joycon_send_subcommand(handle, 0x1, 0x40, buf, 1);
    
    //Read device's S/N
    spi_read(handle, 0x6002, sn_buffer, 0xE);
    
    printf("Successfully initialized %ls with S/N: %c%c%c%c%c%c%c%c%c%c%c%c%c%c!\n", 
        name, sn_buffer[0], sn_buffer[1], sn_buffer[2], sn_buffer[3], 
        sn_buffer[4], sn_buffer[5], sn_buffer[6], sn_buffer[7], sn_buffer[8], 
        sn_buffer[9], sn_buffer[10], sn_buffer[11], sn_buffer[12], 
        sn_buffer[13]);!\n", name);
    
    return 0;
}

void joycon_deinit(hid_device *handle, const wchar_t *name)
{
    unsigned char buf[0x400];
    memset(buf, 0x00, 0x400);

    //Let the Joy-Con talk BT again 
    if(!bluetooth)
    {   
        buf[0] = 0x80;
        buf[1] = 0x05;
        hid_exchange(handle, buf, 0x2);
    }
    
    printf("Deinitialized %ls\n", name);
}

void device_print(struct hid_device_info *dev)
{
    printf("USB device info:\n  vid: 0x%04hX pid: 0x%04hX\n  path: %s\n  MAC: %ls\n  interface_number: %d\n",
        dev->vendor_id, dev->product_id, dev->path, dev->serial_number, dev->interface_number);
    printf("  Manufacturer: %ls\n", dev->manufacturer_string);
    printf("  Product:      %ls\n\n", dev->product_string);
}

int main(int argc, char* argv[])
{
    int res;
    unsigned char buf[2][0x400] = {0};
    hid_device *handle_l = 0, *handle_r = 0;
    const wchar_t *device_name = L"none";
    struct hid_device_info *devs, *dev_iter;
    bool charging_grip = false;
    
    setbuf(stdout, NULL); // turn off stdout buffering for test reasons

    res = hid_init();
    if(res)
    {
        printf("Failed to open hid library! Exiting...\n");
        return -1;
    }

    // iterate thru all the valid product ids and try and initialize controllers
    for(int i = 0; i < sizeof(product_ids); i++)
    {
        devs = hid_enumerate(0x057E, product_ids[i]);
        dev_iter = devs;
        while(dev_iter)
        {
            
            // break out if the current handle is already used
            if((dev_iter->product_id == JOYCON_R_BT || dev_iter->interface_number == 0) && handle_r)
                break;
            else if((dev_iter->product_id == JOYCON_L_BT || dev_iter->interface_number == 1) && handle_l)
                break;
            
            device_print(dev_iter);
            
            if(!wcscmp(dev_iter->serial_number, L"000000000001"))
            {
                bluetooth = false;
            }
            else if(!bluetooth)
            {
                printf("Can't mix USB HID with Bluetooth HID, exiting...\n");
                return -1;
            }
            
            // on windows this will be -1 for devices with one interface
            if(dev_iter->interface_number == 0 || dev_iter->interface_number == -1)
            {
                hid_device *handle = hid_open_path(dev_iter->path);
                if(handle == NULL)
                {
                    printf("Failed to open controller at %ls, continuing...\n", dev_iter->path);
                    dev_iter = dev_iter->next;
                    continue;
                }
                
                switch(dev_iter->product_id)
                {
                    case JOYCON_CHARGING_GRIP:
                        device_name = L"Joy-Con (R)";
                        charging_grip = true;

                        handle_r = handle;
                        break;
                    case PRO_CONTROLLER:
                        device_name = L"Pro Controller";

                        handle_r = handle;
                        break;
                    case JOYCON_L_BT:
                        device_name = L"Joy-Con (L)";

                        handle_l = handle;
                        break;
                    case JOYCON_R_BT:
                        device_name = L"Joy-Con (R)";

                        handle_r = handle;
                        break;
                }
                
                if(joycon_init(handle, device_name))
                {
                    hid_close(handle);
                    if(dev_iter->product_id != JOYCON_L_BT)
                        handle_r = NULL;
                    else
                        handle_l = NULL;
                }
            }
            // Only exists for left Joy-Con in the charging grip
            else if(dev_iter->interface_number == 1)
            {
                handle_l = hid_open_path(dev_iter->path);
                if(handle_l == NULL)
                {
                    printf("Failed to open controller at %ls, continuing...\n", dev_iter->path);
                    dev_iter = dev_iter->next;
                    continue;
                }
                
                if(joycon_init(handle_l, L"Joy-Con (L)"))
                    handle_l = NULL;
            }
            dev_iter = dev_iter->next;
        }
        hid_free_enumeration(devs);
    }
    
    if(!handle_r)
    {
        printf("Failed to get handle for right Joy-Con or Pro Controller, exiting...\n");
        return -1;
    }
    
    // Only missing one half by this point
    if(!handle_l && charging_grip)
    {
        printf("Could not get handles for both Joy-Con in grip! Exiting...\n");
        return -1;
    }
    
    // controller init is complete at this point
    
#ifdef DUMP_SPI
    printf("Dumping controller SPI flashes...\n");
    if(handle_l)
        spi_flash_dump(handle_l, "left_joycon_dump.bin");
    spi_flash_dump(handle_r, "right_joycon_dump.bin");
#endif
    
// Replays a string of hex values (ie 80 92 .. ..) separated by newlines
#ifdef REPLAY
    ssize_t read;
    char *line;
    size_t len = 0;
    FILE *replay = fopen("replay.txt", "rb");
    while ((read = getline(&line, &len, replay)) > 0) {
        int i = 0;
        
        memset(buf[0], 0, 0x400);
        
        char *line_temp = line;
        while(i < 0x400)
        {
            char *last_temp = line_temp;
            buf[0][i++] = strtol(line_temp, &line_temp, 16);
            if(line_temp == last_temp) break;
        }
        if(buf[0][8] == 0x1f) continue; // Cull out input packets

        printf("Sent:     ");
        hex_dump(buf[0], i-1);
        
        if(buf[1])
            memcpy(buf[1], buf[0], 0x400);
            
        int ret_size = hid_dual_exchange(handle_l, handle_r, buf[1], buf[0], i-1);
        printf("Got %03x:  ", ret_size);
        hex_dump(buf[0], ret_size);
        printf("\n");
    }
#endif

#ifdef WEIRD_VIBRATION_TEST
    for(int l = 0x10; l < 0x20; l++)
    {
        for(int i = 0; i < 8; i++)
        {
            for(int k = 0; k < 256; k++)
            {
                memset(buf[0], 0, 0x400);
                for(int j = 0; j <= 8; j++)
                {
                    buf[0][1+i] = 0x1;//(i + j) & 0xFF;
                }
                
                // Set frequency to increase
                buf[0][1+0] = k;
                buf[0][1+4] = k;
                
                if(buf[1])
                    memcpy(buf[1], buf[0], 0x400);
                
                if(handle_l)
                    joycon_send_command(handle_r, 0x10, (uint8_t*)buf, 0x9);
                joycon_send_command(handle_r, 0x10, (uint8_t*)buf, 0x9);
                printf("Sent %x %x %u\n", i & 0xFF, l, k);
            }
        }
    }
#endif
   
   
#ifdef WRITE_TEST
    // Joy-Con color data, body RGB #E8B31C and button RGB #1C1100
    unsigned char color_buffer[6] = {0xE8, 0xB3, 0x5F, 0x1C, 0x11, 0x00};
   
    printf("Changing body color to #%02x%02x%02x, buttons to #%02x%02x%02x\n", 
           color_buffer[0], color_buffer[1], color_buffer[2],
           color_buffer[3], color_buffer[4], color_buffer[5]);
    printf("It's probably safe to exit while this is going, but please wait while it writes...\n");
   
    spi_write(handle_r, 0x6050, color_buffer, 6);
    if(handle_l)
        spi_write(handle_l, 0x6050, color_buffer, 6);
    printf("Writes completed.\n");
#endif

#ifdef LED_TEST
    printf("Enabling some LEDs, sometimes this can fail and take a few times?\n");

    // Player LED Enable
    memset(buf[0], 0x00, 0x400);
    memset(buf[1], 0x00, 0x400);
    buf[0][0] = 0x80 | 0x40 | 0x2 | 0x1; // Flash top two, solid bottom two
    buf[1][0] = 0x80 | 0x40 | 0x2 | 0x1;
    joycon_send_subcommand(handle_r, 0x1, 0x30, buf[0], 1);
    if(handle_l)
        joycon_send_subcommand(handle_l, 0x1, 0x30, buf[1], 1);
    
    // Home LED Enable
    memset(buf[0], 0x00, 0x400);
    memset(buf[1], 0x00, 0x400);
    buf[0][0] = 0xFF; // Slowest pulse?
    buf[1][0] = 0xFF;
    joycon_send_subcommand(handle_r, 0x1, 0x38, buf[0], 1);
    if(handle_l)
        joycon_send_subcommand(handle_l, 0x1, 0x38, buf[1], 1);
#endif
    
#ifdef INPUT_LOOP
    usleep(1000000);
    printf("Start input poll loop\n");
    
    unsigned long last = std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);
    int input_count = 0;
    while(1) {
        printf("%02llums delay,  ", (std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1)) - last);
        last = std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);

        if(!bluetooth)
        {
            if(handle_l)
                joycon_send_command(handle_l, 0x1f, buf[1], 0);
            joycon_send_command(handle_r, 0x1f, buf[0], 0);
        }
        else
        {
            // Fetch more 0x21 input packets        
            if(handle_l)
                joycon_send_subcommand(handle_l, 0x1, 0x0, buf[1], 0);
            joycon_send_subcommand(handle_r, 0x1, 0x0, buf[0], 0);
        }
        
        // USB HID isn't ready
        if(buf[0][0] == 0x30 || buf[1][0] == 0x30 /*|| buf[0][0] == 0x3F || buf[1][0] == 0x3F*/)
        {
            printf("\r");
            continue;
        }
        
        if(handle_l)
        {
            printf("left ");
            hex_dump(buf[1], 0x3D);
            printf("            ");
        }

        printf("right ");
        hex_dump(buf[0], 0x3D);
    }
#endif

    if(handle_l)
    {
        joycon_deinit(handle_l, L"Joy-Con (L)");
        hid_close(handle_l);
    }
    
    if(handle_r)
    {
        joycon_deinit(handle_r, device_name);
        hid_close(handle_r);
    }

    // Finalize the hidapi library
    res = hid_exit();

    return 0;
}
