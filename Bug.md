# Bug Report List
## Level 0
- 2026年1月15日00:18:20 
```
桌面右键菜单无法打开，检查IContextMenu版本问题，升级IContextMenu3后文件操作监听事件正常触发，但是背景右键菜单无法打开
```

### Commont
在调用QueryContextMenu时，传入的参数不正确，应该使用CMF_CANRENAME标志
## Level 1
## Level 2