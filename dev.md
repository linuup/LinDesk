### 直接函数调用实现数据变更通知（回调/观察者模式）

1. **回调函数方式**
   - 在模型类中保存回调函数指针（如std::function<void()>），数据变更时直接调用。
   - 示例：
	 ```cpp
	 #include <functional>
	 class DeskFileModel {
	 public:
		 void setOnChanged(std::function<void()> cb) { m_onChanged = std::move(cb); }
		 void setName(const QString& n) { m_name = n; if (m_onChanged) m_onChanged(); }
	 private:
		 std::function<void()> m_onChanged;
		 QString m_name;
	 };
	 // 使用：
	 model.setOnChanged([&ui]{ ui->update(); });
	 ```

2. **观察者模式（多回调）**
   - 支持多个订阅者：
	 ```cpp
	 #include <vector>
	 class DeskFileModel {
	 public:
		 void addObserver(std::function<void()> cb) { m_observers.push_back(std::move(cb)); }
		 void setName(const QString& n) { m_name = n; for (auto& cb : m_observers) cb(); }
	 private:
		 std::vector<std::function<void()>> m_observers;
		 QString m_name;
	 };
	 ```

3. **注意事项**
   - 回调里不能直接操作UI控件，除非已在主线程。
   - 需管理回调生命周期，防止悬空指针。
### 观察者模式 vs 信号/回调/事件通知的性能对比

- **Qt信号槽**：底层用事件队列和元对象系统，连接和触发有一定开销，但在GUI/多线程场景下安全、灵活，适合Qt项目。
- **回调函数（如std::function）**：直接调用，开销极小，适合简单一对一或一对多场景，但不支持自动线程切换。
- **事件（QEvent）**：通过事件队列传递，适合跨线程和复杂消息分发，性能略低于直接回调。
- **观察者模式（手写订阅/通知）**：本质是回调/接口集合，性能与回调类似，灵活但需手动管理订阅关系。

**性能排序（单线程下）**：回调/观察者 ≈ 直接函数调用 > Qt信号槽 > QEvent。

**实际建议**：
- Qt项目推荐用信号槽，安全、易维护，性能足够。
- 极致性能场景可用回调/观察者，但需注意线程安全和生命周期管理。
- 事件适合跨线程或复杂解耦。
## 数据模型与渲染分离设计方案

### 目标
让模型类（如DeskFileModel）只保存数据，不包含任何界面/绘制逻辑；渲染类（如DeskFileUi）只负责显示和绘制，不持有数据副本。

### 设计建议
1. **模型类只保存数据**
	- 只包含文件名、路径、图标等纯数据成员和getter/setter。
	- 不包含任何QWidget、QPainter等界面相关内容。
	- 可用QAbstractListModel等标准模型接口，便于与Qt视图类对接。

2. **渲染类只负责绘制**
	- 渲染类（如DeskFileUi）通过setModel(const DeskFileModel*)或setData(const DeskFileData&)接口获取数据指针或引用。
	- 渲染类不保存数据副本，只在paintEvent等函数中读取模型数据进行绘制。
	- 渲染类不负责数据的增删改查。

3. **数据变更通知**
	- 若模型数据发生变化，可通过信号/回调/事件通知渲染类刷新（如调用update()）。
	- 也可用观察者模式，模型变更时通知所有视图。

4. **示例接口**
	```cpp
	// 数据模型
	class DeskFileModel {
	public:
		 QString name() const;
		 QString path() const;
		 // ...
	};

	// 渲染类
	class DeskFileUi : public QWidget {
	public:
		 void setModel(const DeskFileModel* model);
	protected:
		 void paintEvent(QPaintEvent*) override;
	private:
		 const DeskFileModel* m_model = nullptr;
	};
	```

5. **优点**
	- 解耦数据与界面，便于单元测试和维护。
	- 支持多视图共享同一数据模型。
	- 便于后续扩展（如MVC/MVVM架构）。


# 这是个桌面管理软件

DeskFileUi.cpp
这个类只绘制用来绘制桌面的文件图标 显示桌面文件的名称，数据通过DeskFileModel类传递过来

## 关于QOpenGLWidget的行为

- QOpenGLWidget始终使用OpenGL（GPU）进行绘制，如果系统或Qt未能创建OpenGL上下文，则会降级为软件渲染或显示空白。
- 如果没有打开GPU加速开关（如设置为Qt::AA_UseSoftwareOpenGL），QOpenGLWidget会尝试用软件模拟OpenGL，但性能较低。
- QWidget及其子类默认用CPU绘制（raster paint engine），只有QOpenGLWidget/QQuickWidget等才用OpenGL。
- 如果你的控件基类是QOpenGLWidget，无论GPU加速开关如何，都会走OpenGL路径，只是可能是软件模拟。
- 若想控件在不开启GPU时自动退回QWidget绘制，需要手动切换控件类型（如用工厂模式创建QWidget或QOpenGLWidget）。


## 桌面文件信息提取静态库接口设计建议

