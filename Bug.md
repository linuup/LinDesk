# Bug Report List
## Level 0
```
1.桌面右键菜单无法打开，检查IContextMenu版本问题，升级IContextMenu3后文件操作监听事件正常触发，但是背景右键菜单无法打开
```
2026年1月15日00:18:20 

### Fixed
在调用QueryContextMenu时，传入的参数不正确，应该使用CMF_CANRENAME标志


```
2.新增Listitem逻辑有问题，导致右键删除文件后,列表中对应的项没有被删除
```
2026年1月18日10:19:53 

### Fixed
已修复

## Level 1
## Level 2