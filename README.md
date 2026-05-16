# Virtual No-Battery Charging Mode
虚拟无电池模式，也经常被叫做旁路充电模式，是 2026 年初手机、平板系统更新中比较热门的一个话题。这个功能可以让设备在某些条件下减少对电池的直接充电，或者绕过电池充电，让系统更直接地从外部电源获取电力。我之前对这个功能的底层框架和实现逻辑做过一些研究，包括它和 Linux/Android 的 power_supply 框架、充电 IC 控制、电量计状态上报，以及系统级充电行为之间的关系。下面的代码是我参与研究/实现的一部分核心功能，主要关注虚拟无电池充电模式的实现，以及它和底层电源管理模块的集成。

<img width="4096" height="3072" alt="虚拟无电池效果" src="https://github.com/user-attachments/assets/10cf1679-a986-4dd3-81d9-03d3c57475b0" />
<img width="4096" height="3072" alt="虚拟无电池效果2" src="https://github.com/user-attachments/assets/b43e7be7-d3a0-4653-a790-4909a255daa5" />
<img width="885" height="183" alt="虚拟无电池3" src="https://github.com/user-attachments/assets/2e3c09c4-5c5b-402e-b9df-c962c218e09b" />
<img width="1603" height="169" alt="虚拟无电池4" src="https://github.com/user-attachments/assets/b8d8f4b9-131e-4314-9959-a1c23f520737" />
