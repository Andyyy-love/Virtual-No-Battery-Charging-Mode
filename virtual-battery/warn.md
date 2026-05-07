## 1.电源适配器软件控制断开已实现，但存在bug
目前bug:拔掉电源后，hiz_enable状态会复位成0
```c
TB311FU:/sys/class/power_supply/sgm4154x-charger # ls
charge_type  device  hiz_enable           manufacturer  online  present  subsystem  uevent    voltage_now
current_now  health  input_current_limit  model_name    power   status   type       usb_type  wakeup14
TB311FU:/sys/class/power_supply/sgm4154x-charger # cat hiz_enable
0
TB311FU:/sys/class/power_supply/sgm4154x-charger # echo 1 > hiz_enable
TB311FU:/sys/class/power_supply/sgm4154x-charger # cat hiz_enable
1
```
## 2、解决禁用电池充电功能问题
已经实现！
## 目前实现的功能
### 1、mm8013电量计芯片的nvm寄存器读写
```c
TB311FU:/sys/class/power_supply/mm8013c10-0/device # cat bypass_setting
105020
TB311FU:/sys/class/power_supply/mm8013c10-0/device # ls
activate_date   charging_enabled  input_suspend  name     power         produce_date  subsystem
bypass_setting  driver            modalias       of_node  power_supply  soh           uevent
TB311FU:/sys/class/power_supply/mm8013c10-0/device # echo 105020 > bypass_setting
TB311FU:/sys/class/power_supply/mm8013c10-0/device # cat bypass_setting
105020
TB311FU:/sys/class/power_supply/mm8013c10-0/device #
```
### 2、 sgm415xx芯片禁用电池充电
charging_enable
- 0:禁止电池充电，但仍给主板供电
- 1:正常充电
```c
TB311FU:/sys/class/power_supply/sgm4154x-charger # ls
charge_type      device      input_current_limit  online   status     uevent       wakeup14
charging_enable  health      manufacturer         power    subsystem  usb_type
current_now      hiz_enable  model_name           present  type       voltage_now
TB311FU:/sys/class/power_supply/sgm4154x-charger # cat charging_enable
1

```

### 3、sgm415xx芯片软件控制电源适配器断开
- 0：正常链接适配器
- 1：软件控制断开适配器
```c
TB311FU:/sys/class/power_supply/sgm4154x-charger # cat hiz_enable
0
```

## 文件夹备注
```c
vitural_battery_cn_kernel_new.h
const unsigned char g_word_logo_argb[] = 

vitural_battery_en_kernel_new.h
const unsigned char g_word_logo_argb_en[] =

vitural_battery_new.h
const unsigned char g_logo_argb[] = 
```


## 几个.h文件对应开机图片
const unsigned char g_logo_argb[]
const unsigned char g_word_logo_argb[]
const unsigned char g_word_logo_argb_en[]

## 关机充电本质
关机充电时，当插入typec口，此时会进入lk阶段，然后进linux内核，开始关机充电，android上层未启动


## 写mmc
```c
C:\Users\PC>adb shell
TB311FU:/ $ ls -l /dev/block/by-name/proinfo
lrwxrwxrwx 1 root root 21 2026-02-24 20:19 /dev/block/by-name/proinfo -> /dev/block/mmcblk0p36
TB311FU:/ $ su
TB311FU:/ # echo -n -e '\x01' | dd of=/dev/block/mmcblk0p35 bs=1 seek=35 conv=notrunc
1+0 records in
1+0 records out
1 bytes (1 B) copied, 0.001 s, 0.9 K/s
TB311FU:/ #   echo -n -e '\x01' | dd of=/dev/block/mmcblk0p36 bs=1 seek=35 conv=notrunc
1+0 records in
1+0 records out
1 bytes (1 B) copied, 0.003 s, 333 B/s
TB311FU:/ # reboot

C:\Users\PC>
```