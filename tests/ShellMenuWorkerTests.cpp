#include <gtest/gtest.h>
#include <QCoreApplication>
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <future>
#include <chrono>
#include <atomic>
#include <vector>
#include <thread>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QDir>
#include <QStandardPaths>

#include "services/SystemApi/ShellMenuTypes.h"
#include "services/SystemApi/ShellMenuWorker.h"

class ShellMenuWorkerTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        if (!qApp) {
            static int argc = 1;
            static char appName[] = "ShellMenuWorkerTest";
            static char* argv[] = { appName, nullptr };
            new QCoreApplication(argc, argv);
        }
    }
};

// -----------------------------------------------------------------------------
// 1. Worker thread initializes STA apartment successfully
// -----------------------------------------------------------------------------
TEST_F(ShellMenuWorkerTest, STA_ApartmentInitializedSuccessfully)
{
    ShellMenuWorker worker;
    ASSERT_TRUE(worker.startWorker());
    ASSERT_TRUE(worker.isWorkerRunning());

    std::promise<HRESULT> hrPromise;
    std::promise<APTTYPE> aptTypePromise;
    std::promise<DWORD> tidPromise;

    auto hrFuture = hrPromise.get_future();
    auto aptTypeFuture = aptTypePromise.get_future();
    auto tidFuture = tidPromise.get_future();

    worker.postTask([&]() {
        APTTYPE aptType = APTTYPE_CURRENT;
        APTTYPEQUALIFIER aptQual = APTTYPEQUALIFIER_NONE;
        HRESULT hr = CoGetApartmentType(&aptType, &aptQual);
        hrPromise.set_value(hr);
        aptTypePromise.set_value(aptType);
        tidPromise.set_value(GetCurrentThreadId());
    });

    ASSERT_EQ(hrFuture.wait_for(std::chrono::seconds(3)), std::future_status::ready);
    EXPECT_EQ(hrFuture.get(), S_OK);

    // APTTYPE_STA confirms Single-Threaded Apartment
    APTTYPE apt = aptTypeFuture.get();
    EXPECT_TRUE(apt == APTTYPE_STA || apt == APTTYPE_MAINSTA);

    // Verify it runs on the dedicated worker thread, not test thread
    DWORD workerTid = tidFuture.get();
    EXPECT_EQ(workerTid, worker.workerThreadId());
    EXPECT_NE(workerTid, GetCurrentThreadId());

    worker.stopWorker();
}




// -----------------------------------------------------------------------------
// 2. Worker message HWND is created and processes messages
// -----------------------------------------------------------------------------
TEST_F(ShellMenuWorkerTest, HostWindow_CreatedAndPumpsMessages)
{
    ShellMenuWorker worker;
    ASSERT_TRUE(worker.startWorker());

    HWND hwnd = worker.hostHwnd();
    ASSERT_NE(hwnd, nullptr);
    EXPECT_TRUE(IsWindow(hwnd));

    // Verify window thread ownership
    DWORD windowTid = GetWindowThreadProcessId(hwnd, nullptr);
    EXPECT_EQ(windowTid, worker.workerThreadId());

    // Inter-thread SendMessage dispatches through worker's message pump
    constexpr UINT WM_TEST_PING = WM_USER + 102;
    LRESULT response = SendMessageW(hwnd, WM_TEST_PING, 100, 200);
    EXPECT_EQ(response, 300);

    // Test safe handling of menu messages (e.g. WM_INITMENUPOPUP without crashing)
    LRESULT menuInitResult = SendMessageW(hwnd, WM_INITMENUPOPUP, 0, 0);
    EXPECT_EQ(menuInitResult, 0);

    worker.stopWorker();
}




// -----------------------------------------------------------------------------
// 3. Posting asynchronous query tasks executes without deadlocking or crashing
// -----------------------------------------------------------------------------
TEST_F(ShellMenuWorkerTest, AsyncTasks_ExecuteWithoutDeadlock)
{
    ShellMenuWorker worker;
    ASSERT_TRUE(worker.startWorker());

    // Sequential task test
    std::promise<int> p1;
    auto f1 = p1.get_future();
    worker.postTask([&]() {
        p1.set_value(42);
    });
    ASSERT_EQ(f1.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_EQ(f1.get(), 42);

    // Batch concurrent task test (50 tasks)
    constexpr int TASK_COUNT = 50;
    std::atomic<int> completedTasks{ 0 };
    std::promise<void> allDonePromise;
    auto allDoneFuture = allDonePromise.get_future();

    for (int i = 0; i < TASK_COUNT; ++i) {
        worker.postTask([&, i]() {
            int current = ++completedTasks;
            if (current == TASK_COUNT) {
                allDonePromise.set_value();
            }
        });
    }

    ASSERT_EQ(allDoneFuture.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_EQ(completedTasks.load(), TASK_COUNT);

    // High-level request posting test
    ShellMenuRequest req;
    req.requestId = 777;
    req.targetType = ShellTargetType::DesktopBackground;

    std::promise<uint64_t> resultPromise;
    auto resultFuture = resultPromise.get_future();

    worker.postRequest(req, [&](const ShellMenuResult& res) {
        resultPromise.set_value(res.requestId);
    });

    ASSERT_EQ(resultFuture.wait_for(std::chrono::seconds(3)), std::future_status::ready);
    EXPECT_EQ(resultFuture.get(), 777ULL);

    worker.stopWorker();
}




// -----------------------------------------------------------------------------
// 4. Graceful shutdown of worker thread
// -----------------------------------------------------------------------------
TEST_F(ShellMenuWorkerTest, GracefulShutdown_TerminatesCleanlyAndDestroysWindow)
{
    HWND capturedHwnd = nullptr;

    {
        ShellMenuWorker worker;
        ASSERT_TRUE(worker.startWorker());
        capturedHwnd = worker.hostHwnd();
        EXPECT_TRUE(IsWindow(capturedHwnd));

        // Explicit stopWorker call
        worker.stopWorker();
        EXPECT_FALSE(worker.isWorkerRunning());
        EXPECT_FALSE(IsWindow(capturedHwnd));

        // Idempotent stop test
        worker.stopWorker();
    }

    // RAII destructor scope exit test
    HWND scopedHwnd = nullptr;
    {
        ShellMenuWorker worker;
        ASSERT_TRUE(worker.startWorker());
        scopedHwnd = worker.hostHwnd();
        EXPECT_TRUE(IsWindow(scopedHwnd));
    }
    // After destructor runs:
    EXPECT_FALSE(IsWindow(scopedHwnd));
}

// -----------------------------------------------------------------------------
// 5. COM Shell object binding inside worker STA apartment
// -----------------------------------------------------------------------------
TEST_F(ShellMenuWorkerTest, ShellComBinding_SucceedsInSTA)
{
    ShellMenuWorker worker;
    ASSERT_TRUE(worker.startWorker());

    std::promise<bool> bindPromise;
    auto bindFuture = bindPromise.get_future();

    worker.postTask([&]() {
        IShellFolder* pDesktop = nullptr;
        HRESULT hr = SHGetDesktopFolder(&pDesktop);
        if (SUCCEEDED(hr) && pDesktop != nullptr) {
            pDesktop->Release();
            bindPromise.set_value(true);
        } else {
            bindPromise.set_value(false);
        }
    });

    ASSERT_EQ(bindFuture.wait_for(std::chrono::seconds(3)), std::future_status::ready);
    EXPECT_TRUE(bindFuture.get());

    worker.stopWorker();
}




// -----------------------------------------------------------------------------
// 6. Stress: Rapid concurrent request posting (150 requests from 10 threads)
// -----------------------------------------------------------------------------
TEST_F(ShellMenuWorkerTest, Stress_RapidConcurrentRequestBurst_100Plus)
{
    ShellMenuWorker worker;
    ASSERT_TRUE(worker.startWorker());

    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());
    QString filePath = tempDir.filePath("test_target.txt");
    QFile file(filePath);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("Hello ShellMenuWorker Stress Test");
    file.close();

    constexpr int NUM_THREADS = 10;
    constexpr int REQS_PER_THREAD = 15;
    constexpr int TOTAL_REQUESTS = NUM_THREADS * REQS_PER_THREAD; // 150 requests

    std::atomic<int> completedCallbacks{ 0 };
    std::promise<void> allCompletedPromise;
    auto allCompletedFuture = allCompletedPromise.get_future();

    std::vector<std::thread> clientThreads;
    clientThreads.reserve(NUM_THREADS);

    auto startTime = std::chrono::steady_clock::now();

    for (int t = 0; t < NUM_THREADS; ++t) {
        clientThreads.emplace_back([&, t]() {
            for (int r = 0; r < REQS_PER_THREAD; ++r) {
                uint64_t reqId = 10000 + t * 100 + r;
                ShellMenuRequest req;
                req.requestId = reqId;
                if (r % 3 == 0) {
                    req.targetType = ShellTargetType::DesktopBackground;
                } else if (r % 3 == 1) {
                    req.targetType = ShellTargetType::File;
                    req.targetPath = filePath;
                } else {
                    req.targetType = ShellTargetType::Folder;
                    req.targetPath = tempDir.path();
                }
                req.showMenu = false;

                worker.postRequest(req, [&, reqId](const ShellMenuResult& res) {
                    EXPECT_EQ(res.requestId, reqId);
                    int count = ++completedCallbacks;
                    if (count == TOTAL_REQUESTS) {
                        allCompletedPromise.set_value();
                    }
                });
            }
        });
    }

    for (auto& th : clientThreads) {
        th.join();
    }

    auto status = allCompletedFuture.wait_for(std::chrono::seconds(45));
    auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime).count();

    std::cout << "[DIAGNOSTIC] Stress_RapidConcurrentRequestBurst: completed="
              << completedCallbacks.load() << " / " << TOTAL_REQUESTS
              << " in " << durationMs << " ms" << std::endl;

    ASSERT_EQ(status, std::future_status::ready);
    EXPECT_EQ(completedCallbacks.load(), TOTAL_REQUESTS);

    worker.stopWorker();
}




// -----------------------------------------------------------------------------
// 7. Stress: Rapid request cancellation under heavy load
// -----------------------------------------------------------------------------
TEST_F(ShellMenuWorkerTest, Stress_RapidCancellationUnderHeavyLoad)
{
    ShellMenuWorker worker;
    ASSERT_TRUE(worker.startWorker());

    constexpr int TOTAL_REQS = 120;
    std::atomic<int> callbackCount{ 0 };

    for (int i = 0; i < TOTAL_REQS; ++i) {
        ShellMenuRequest req;
        req.requestId = 20000 + i;
        req.targetType = ShellTargetType::DesktopBackground;
        req.showMenu = false;

        worker.postRequest(req, [&](const ShellMenuResult&) {
            callbackCount++;
        });
    }

    std::vector<std::thread> cancellers;
    for (int c = 0; c < 4; ++c) {
        cancellers.emplace_back([&, c]() {
            for (int i = c * 15; i < (c + 1) * 15; ++i) {
                uint64_t cancelId = 20000 + (i * 2);
                worker.cancelRequest(cancelId);
            }
        });
    }

    for (auto& t : cancellers) {
        t.join();
    }

    std::promise<bool> sentinelPromise;
    auto sentinelFuture = sentinelPromise.get_future();
    worker.postTask([&]() {
        sentinelPromise.set_value(true);
    });

    ASSERT_EQ(sentinelFuture.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_TRUE(sentinelFuture.get());

    worker.stopWorker();
    EXPECT_FALSE(worker.isWorkerRunning());
}

// -----------------------------------------------------------------------------
// 8. Stress: Thread shutdown under heavy pending request load
// -----------------------------------------------------------------------------
TEST_F(ShellMenuWorkerTest, Stress_ShutdownUnderHeavyLoadWithPendingRequests)
{
    for (int cycle = 0; cycle < 5; ++cycle) {
        ShellMenuWorker worker;
        ASSERT_TRUE(worker.startWorker());
        HWND hwnd = worker.hostHwnd();
        ASSERT_NE(hwnd, nullptr);

        for (int i = 0; i < 50; ++i) {
            ShellMenuRequest req;
            req.requestId = 30000 + cycle * 100 + i;
            req.targetType = ShellTargetType::DesktopBackground;
            req.showMenu = false;
            worker.postRequest(req, [](const ShellMenuResult&) {});
        }

        auto stopStart = std::chrono::steady_clock::now();
        worker.stopWorker();
        auto stopElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - stopStart).count();

        std::cout << "[DIAGNOSTIC] cycle " << cycle << " stopElapsed=" << stopElapsed
                  << " ms, isRunning=" << worker.isWorkerRunning()
                  << " IsWindow=" << (IsWindow(hwnd) ? "TRUE (LEAKED)" : "FALSE") << std::endl;

        EXPECT_FALSE(worker.isWorkerRunning());
        EXPECT_FALSE(IsWindow(hwnd));
        EXPECT_LT(stopElapsed, 3500);
    }
}

// -----------------------------------------------------------------------------
// 9. Stress: Rapid start/stop lifecycle cycles
// -----------------------------------------------------------------------------
TEST_F(ShellMenuWorkerTest, Stress_RapidStartStopCycles)
{
    for (int cycle = 0; cycle < 15; ++cycle) {
        ShellMenuWorker worker;
        ASSERT_TRUE(worker.startWorker());
        EXPECT_TRUE(worker.isWorkerRunning());
        HWND hwnd = worker.hostHwnd();
        EXPECT_TRUE(IsWindow(hwnd));

        std::promise<int> p;
        auto f = p.get_future();
        worker.postTask([&p, cycle]() {
            p.set_value(cycle);
        });
        ASSERT_EQ(f.wait_for(std::chrono::seconds(2)), std::future_status::ready);
        EXPECT_EQ(f.get(), cycle);

        worker.stopWorker();
        EXPECT_FALSE(worker.isWorkerRunning());
        EXPECT_FALSE(IsWindow(hwnd));
    }
}

// -----------------------------------------------------------------------------
// 10. Stress: Active modal tracking menu cancellation
// -----------------------------------------------------------------------------
TEST_F(ShellMenuWorkerTest, Stress_ActiveTrackingMenuCancellation)
{
    ShellMenuWorker worker;
    ASSERT_TRUE(worker.startWorker());

    std::promise<bool> dismissedPromise;
    auto dismissedFuture = dismissedPromise.get_future();

    ShellMenuRequest req;
    req.requestId = 40001;
    req.targetType = ShellTargetType::DesktopBackground;
    req.showMenu = true;
    req.globalPos = QPoint(50, 50);

    std::atomic<bool> sawTracking{ false };
    std::thread canceller([&]() {
        for (int i = 0; i < 50; ++i) {
            if (worker.isTrackingMenu()) {
                sawTracking = true;
                std::cout << "[DIAGNOSTIC] saw isTrackingMenu == true at iteration " << i << std::endl;
                worker.cancelActiveMenu();
                // PostMessageW(worker.hostHwnd(), WM_CANCELMODE, 0, 0);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        std::cout << "[DIAGNOSTIC] timed out waiting for isTrackingMenu == true" << std::endl;
        worker.cancelActiveMenu();
    });

    worker.postRequest(req, [&](const ShellMenuResult& res) {
        std::cout << "[DIAGNOSTIC] Menu callback invoked: executed=" << res.executed << std::endl;
        EXPECT_EQ(res.requestId, 40001ULL);
        EXPECT_FALSE(res.executed);
        dismissedPromise.set_value(true);
    });

    auto waitRes = dismissedFuture.wait_for(std::chrono::seconds(5));
    std::cout << "[DIAGNOSTIC] dismissedFuture wait result: " << (waitRes == std::future_status::ready ? "READY" : "TIMEOUT")
              << " sawTracking=" << sawTracking.load()
              << " isTrackingMenu=" << worker.isTrackingMenu() << std::endl;

    ASSERT_EQ(waitRes, std::future_status::ready);
    EXPECT_TRUE(dismissedFuture.get());
    EXPECT_FALSE(worker.isTrackingMenu());

    canceller.join();
    worker.stopWorker();
}




// -----------------------------------------------------------------------------
// 10b. Stress: Immediate menu cancellation during query before modal tracking
// -----------------------------------------------------------------------------
TEST_F(ShellMenuWorkerTest, Stress_PreTrackingMenuCancellationDuringQuery)
{
    ShellMenuWorker worker;
    ASSERT_TRUE(worker.startWorker());

    std::promise<bool> dismissedPromise;
    auto dismissedFuture = dismissedPromise.get_future();

    ShellMenuRequest req;
    req.requestId = 40002;
    req.targetType = ShellTargetType::DesktopBackground;
    req.showMenu = true;
    req.globalPos = QPoint(100, 100);

    worker.postRequest(req, [&](const ShellMenuResult& res) {
        EXPECT_EQ(res.requestId, 40002ULL);
        EXPECT_FALSE(res.executed);
        dismissedPromise.set_value(true);
    });

    // Immediately cancel from caller thread before SafeQueryContextMenu finishes
    worker.cancelActiveMenu();

    ASSERT_EQ(dismissedFuture.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_TRUE(dismissedFuture.get());
    EXPECT_FALSE(worker.isTrackingMenu());

    worker.stopWorker();
}




// -----------------------------------------------------------------------------
// 10c. Stress: Direct Win32 WM_CANCELMODE message posted to host window
// -----------------------------------------------------------------------------
TEST_F(ShellMenuWorkerTest, Stress_DirectWin32CancelModeMessageToHostWindow)
{
    ShellMenuWorker worker;
    ASSERT_TRUE(worker.startWorker());

    std::promise<bool> dismissedPromise;
    auto dismissedFuture = dismissedPromise.get_future();

    ShellMenuRequest req;
    req.requestId = 40003;
    req.targetType = ShellTargetType::DesktopBackground;
    req.showMenu = true;
    req.globalPos = QPoint(150, 150);

    std::thread poster([&]() {
        for (int i = 0; i < 100; ++i) {
            if (worker.isTrackingMenu()) {
                PostMessageW(worker.hostHwnd(), WM_CANCELMODE, 0, 0);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        PostMessageW(worker.hostHwnd(), WM_CANCELMODE, 0, 0);
    });

    worker.postRequest(req, [&](const ShellMenuResult& res) {
        EXPECT_EQ(res.requestId, 40003ULL);
        EXPECT_FALSE(res.executed);
        dismissedPromise.set_value(true);
    });

    ASSERT_EQ(dismissedFuture.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_TRUE(dismissedFuture.get());
    EXPECT_FALSE(worker.isTrackingMenu());

    poster.join();
    worker.stopWorker();
}




// -----------------------------------------------------------------------------
// 10d. Stress: Concurrent burst of cancelActiveMenu calls from multiple threads
// -----------------------------------------------------------------------------
TEST_F(ShellMenuWorkerTest, Stress_CancelActiveMenu_ConcurrentBurst)
{
    ShellMenuWorker worker;
    ASSERT_TRUE(worker.startWorker());

    std::promise<bool> dismissedPromise;
    auto dismissedFuture = dismissedPromise.get_future();

    ShellMenuRequest req;
    req.requestId = 40004;
    req.targetType = ShellTargetType::DesktopBackground;
    req.showMenu = true;
    req.globalPos = QPoint(200, 200);

    worker.postRequest(req, [&](const ShellMenuResult& res) {
        EXPECT_EQ(res.requestId, 40004ULL);
        EXPECT_FALSE(res.executed);
        dismissedPromise.set_value(true);
    });

    constexpr int THREAD_COUNT = 8;
    std::vector<std::thread> threads;
    for (int t = 0; t < THREAD_COUNT; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < 20; ++i) {
                worker.cancelActiveMenu();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(dismissedFuture.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_TRUE(dismissedFuture.get());
    EXPECT_FALSE(worker.isTrackingMenu());

    worker.stopWorker();
}




// -----------------------------------------------------------------------------
// 11. Stress: Adversarial target types and corrupted/boundary inputs
// -----------------------------------------------------------------------------
TEST_F(ShellMenuWorkerTest, Stress_AdversarialTargetTypesAndCorruptedInputs)
{
    ShellMenuWorker worker;
    ASSERT_TRUE(worker.startWorker());

    std::vector<ShellMenuRequest> adversarialRequests;

    // 1. Empty target path for File
    {
        ShellMenuRequest r;
        r.requestId = 50001;
        r.targetType = ShellTargetType::File;
        r.targetPath = "";
        adversarialRequests.push_back(r);
    }
    // 2. Non-existent path
    {
        ShellMenuRequest r;
        r.requestId = 50002;
        r.targetType = ShellTargetType::File;
        r.targetPath = "C:\\LinDesk_NonExistent_Test_Path_12345\\dummy.txt";
        adversarialRequests.push_back(r);
    }
    // 3. Path with invalid characters
    {
        ShellMenuRequest r;
        r.requestId = 50003;
        r.targetType = ShellTargetType::File;
        r.targetPath = "D:\\<invalid>:\"|?*test.txt";
        adversarialRequests.push_back(r);
    }
    // 4. Overlong path (>300 chars)
    {
        ShellMenuRequest r;
        r.requestId = 50004;
        r.targetType = ShellTargetType::File;
        r.targetPath = "C:\\" + QString("a").repeated(320) + ".txt";
        adversarialRequests.push_back(r);
    }
    // 5. Unknown special folder
    {
        ShellMenuRequest r;
        r.requestId = 50005;
        r.targetType = ShellTargetType::SpecialFolder;
        r.specialFolder = SpecialFolders::UnknownFolder;
        adversarialRequests.push_back(r);
    }
    // 6. MultiObject with empty paths
    {
        ShellMenuRequest r;
        r.requestId = 50006;
        r.targetType = ShellTargetType::MultiObject;
        r.multiPaths = {};
        adversarialRequests.push_back(r);
    }
    // 7. MultiObject with non-existent paths
    {
        ShellMenuRequest r;
        r.requestId = 50007;
        r.targetType = ShellTargetType::MultiObject;
        r.multiPaths = { "C:\\no1.tmp", "C:\\no2.tmp" };
        adversarialRequests.push_back(r);
    }
    // 8. Custom item with null callback and empty text
    {
        ShellMenuRequest r;
        r.requestId = 50008;
        r.targetType = ShellTargetType::DesktopBackground;
        ShellCustomMenuItem item;
        item.text = L"";
        item.callback = nullptr;
        r.customItems.push_back(item);
        adversarialRequests.push_back(r);
    }

    std::atomic<int> completedCount{ 0 };
    std::promise<void> allDone;
    auto future = allDone.get_future();
    const int total = static_cast<int>(adversarialRequests.size());

    for (const auto& req : adversarialRequests) {
        worker.postRequest(req, [&, total](const ShellMenuResult&) {
            if (++completedCount == total) {
                allDone.set_value();
            }
        });
    }

    ASSERT_EQ(future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_EQ(completedCount.load(), total);

    worker.stopWorker();
}




// -----------------------------------------------------------------------------
// 12. Stress: Handle leak verification (USER and GDI resources)
// -----------------------------------------------------------------------------
TEST_F(ShellMenuWorkerTest, Stress_HandleLeakVerification)
{
    ShellMenuWorker worker;
    ASSERT_TRUE(worker.startWorker());

    // Warm-up queries (40 runs to allow lazy shell extensions and COM caches to initialize to steady state)
    for (int w = 0; w < 40; ++w) {
        std::promise<void> warmupPromise;
        auto warmupFuture = warmupPromise.get_future();
        ShellMenuRequest req;
        req.requestId = 60000 + w;
        req.targetType = ShellTargetType::DesktopBackground;
        worker.postRequest(req, [&](const ShellMenuResult&) {
            warmupPromise.set_value();
        });
        ASSERT_EQ(warmupFuture.wait_for(std::chrono::seconds(3)), std::future_status::ready);
    }

    // Ensure worker thread is completely idle after warm-up
    {
        std::promise<void> p;
        auto f = p.get_future();
        worker.postTask([&p]() { p.set_value(); });
        ASSERT_EQ(f.wait_for(std::chrono::seconds(3)), std::future_status::ready);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    HANDLE hProc = GetCurrentProcess();
    DWORD gdiBefore = GetGuiResources(hProc, GR_GDIOBJECTS);
    DWORD userBefore = GetGuiResources(hProc, GR_USEROBJECTS);

    constexpr int RUN_COUNT = 80;
    std::atomic<int> completed{ 0 };
    std::promise<void> allDonePromise;
    auto allDoneFuture = allDonePromise.get_future();

    for (int i = 0; i < RUN_COUNT; ++i) {
        ShellMenuRequest req;
        req.requestId = 60001 + i;
        req.targetType = ShellTargetType::DesktopBackground;
        worker.postRequest(req, [&, i](const ShellMenuResult&) {
            int c = ++completed;
            if (c == 1 || c == 2 || c == 5 || c == 10 || c == 40 || c == 80) {
                DWORD u = GetGuiResources(hProc, GR_USEROBJECTS);
                std::cout << "[USER SAMPLE] completed=" << c << " user=" << u << std::endl;
            }
            if (c == RUN_COUNT) {
                allDonePromise.set_value();
            }
        });
    }

    ASSERT_EQ(allDoneFuture.wait_for(std::chrono::seconds(20)), std::future_status::ready);
    EXPECT_EQ(completed.load(), RUN_COUNT);

    // Ensure worker thread has completely finished unwinding all 80 requests
    {
        std::promise<void> p;
        auto f = p.get_future();
        worker.postTask([&p]() { p.set_value(); });
        ASSERT_EQ(f.wait_for(std::chrono::seconds(3)), std::future_status::ready);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    DWORD gdiAfter = GetGuiResources(hProc, GR_GDIOBJECTS);
    DWORD userAfter = GetGuiResources(hProc, GR_USEROBJECTS);

    std::cout << "[DIAGNOSTIC] gdiBefore=" << gdiBefore << " gdiAfter=" << gdiAfter
              << " diff=" << (static_cast<long>(gdiAfter) - static_cast<long>(gdiBefore))
              << " | userBefore=" << userBefore << " userAfter=" << userAfter
              << " diff=" << (static_cast<long>(userAfter) - static_cast<long>(userBefore)) << std::endl;

    EXPECT_LE(static_cast<long>(gdiAfter) - static_cast<long>(gdiBefore), 2);
    // Allow up to 15 handles for OS-level shell icon/theme cache expansion under multi-test CTest load,
    // while strictly guarding against per-request HMENU leaks (which would leak >= 80 handles).
    EXPECT_LE(static_cast<long>(userAfter) - static_cast<long>(userBefore), 15);

    worker.stopWorker();
}




// -----------------------------------------------------------------------------
// 12a. Dedicated DestroyMenuAndBitmaps Unit Tests
// -----------------------------------------------------------------------------
TEST_F(ShellMenuWorkerTest, DestroyMenuAndBitmaps_NullAndInvalidHandleSafety)
{
    EXPECT_FALSE(ShellMenuWorker::DestroyMenuAndBitmaps(nullptr));
    EXPECT_FALSE(ShellMenuWorker::DestroyMenuAndBitmaps(reinterpret_cast<HMENU>(static_cast<uintptr_t>(0xDEADBEEF))));
}

TEST_F(ShellMenuWorkerTest, DestroyMenuAndBitmaps_DeletesAttachedBitmapsAndDestroysMenu)
{
    HMENU hMenu = CreatePopupMenu();
    ASSERT_NE(hMenu, nullptr);

    HBITMAP bmp1 = CreateBitmap(16, 16, 1, 32, nullptr);
    HBITMAP bmp2 = CreateBitmap(16, 16, 1, 32, nullptr);
    ASSERT_NE(bmp1, nullptr);
    ASSERT_NE(bmp2, nullptr);

    EXPECT_EQ(GetObjectType(bmp1), OBJ_BITMAP);
    EXPECT_EQ(GetObjectType(bmp2), OBJ_BITMAP);

    MENUITEMINFOW mii1 = {};
    mii1.cbSize = sizeof(mii1);
    mii1.fMask = MIIM_STRING | MIIM_ID | MIIM_BITMAP;
    mii1.wID = 101;
    mii1.dwTypeData = const_cast<LPWSTR>(L"Item 1");
    mii1.hbmpItem = bmp1;
    ASSERT_TRUE(InsertMenuItemW(hMenu, 0, TRUE, &mii1));

    MENUITEMINFOW mii2 = {};
    mii2.cbSize = sizeof(mii2);
    mii2.fMask = MIIM_STRING | MIIM_ID | MIIM_BITMAP;
    mii2.wID = 102;
    mii2.dwTypeData = const_cast<LPWSTR>(L"Item 2");
    mii2.hbmpItem = bmp2;
    ASSERT_TRUE(InsertMenuItemW(hMenu, 1, TRUE, &mii2));

    EXPECT_TRUE(ShellMenuWorker::DestroyMenuAndBitmaps(hMenu));
    EXPECT_FALSE(IsMenu(hMenu));

    // Confirm that both bitmaps were freed
    EXPECT_FALSE(DeleteObject(bmp1)); // Already deleted
    EXPECT_FALSE(DeleteObject(bmp2)); // Already deleted
}

TEST_F(ShellMenuWorkerTest, DestroyMenuAndBitmaps_DeletesNestedSubmenuBitmaps)
{
    HMENU hRoot = CreatePopupMenu();
    HMENU hSub = CreatePopupMenu();
    ASSERT_NE(hRoot, nullptr);
    ASSERT_NE(hSub, nullptr);

    HBITMAP rootBmp = CreateBitmap(16, 16, 1, 32, nullptr);
    HBITMAP subBmp = CreateBitmap(16, 16, 1, 32, nullptr);

    MENUITEMINFOW miiSubItem = {};
    miiSubItem.cbSize = sizeof(miiSubItem);
    miiSubItem.fMask = MIIM_STRING | MIIM_ID | MIIM_BITMAP;
    miiSubItem.wID = 201;
    miiSubItem.dwTypeData = const_cast<LPWSTR>(L"Sub Item");
    miiSubItem.hbmpItem = subBmp;
    ASSERT_TRUE(InsertMenuItemW(hSub, 0, TRUE, &miiSubItem));

    MENUITEMINFOW miiRootItem = {};
    miiRootItem.cbSize = sizeof(miiRootItem);
    miiRootItem.fMask = MIIM_STRING | MIIM_SUBMENU | MIIM_BITMAP;
    miiRootItem.dwTypeData = const_cast<LPWSTR>(L"Root Menu");
    miiRootItem.hSubMenu = hSub;
    miiRootItem.hbmpItem = rootBmp;
    ASSERT_TRUE(InsertMenuItemW(hRoot, 0, TRUE, &miiRootItem));

    EXPECT_TRUE(ShellMenuWorker::DestroyMenuAndBitmaps(hRoot));
    EXPECT_FALSE(IsMenu(hRoot));
    EXPECT_FALSE(IsMenu(hSub));

    EXPECT_FALSE(DeleteObject(rootBmp));
    EXPECT_FALSE(DeleteObject(subBmp));
}

TEST_F(ShellMenuWorkerTest, DestroyMenuAndBitmaps_IgnoresSystemBitmaps)
{
    HMENU hMenu = CreatePopupMenu();
    ASSERT_NE(hMenu, nullptr);

    MENUITEMINFOW mii = {};
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_STRING | MIIM_ID | MIIM_BITMAP;
    mii.wID = 301;
    mii.dwTypeData = const_cast<LPWSTR>(L"Close Item");
    mii.hbmpItem = HBMMENU_POPUP_CLOSE;
    ASSERT_TRUE(InsertMenuItemW(hMenu, 0, TRUE, &mii));

    mii.wID = 302;
    mii.dwTypeData = const_cast<LPWSTR>(L"Callback Item");
    mii.hbmpItem = HBMMENU_CALLBACK;
    ASSERT_TRUE(InsertMenuItemW(hMenu, 1, TRUE, &mii));

    EXPECT_TRUE(ShellMenuWorker::DestroyMenuAndBitmaps(hMenu));
    EXPECT_FALSE(IsMenu(hMenu));
}

TEST_F(ShellMenuWorkerTest, DestroyMenuAndBitmaps_DeduplicatesSharedBitmaps)
{
    HMENU hMenu = CreatePopupMenu();
    ASSERT_NE(hMenu, nullptr);

    HBITMAP sharedBmp = CreateBitmap(16, 16, 1, 32, nullptr);
    ASSERT_NE(sharedBmp, nullptr);

    MENUITEMINFOW mii1 = {};
    mii1.cbSize = sizeof(mii1);
    mii1.fMask = MIIM_STRING | MIIM_ID | MIIM_BITMAP;
    mii1.wID = 401;
    mii1.dwTypeData = const_cast<LPWSTR>(L"Shared 1");
    mii1.hbmpItem = sharedBmp;
    ASSERT_TRUE(InsertMenuItemW(hMenu, 0, TRUE, &mii1));

    MENUITEMINFOW mii2 = {};
    mii2.cbSize = sizeof(mii2);
    mii2.fMask = MIIM_STRING | MIIM_ID | MIIM_BITMAP;
    mii2.wID = 402;
    mii2.dwTypeData = const_cast<LPWSTR>(L"Shared 2");
    mii2.hbmpItem = sharedBmp;
    ASSERT_TRUE(InsertMenuItemW(hMenu, 1, TRUE, &mii2));

    EXPECT_TRUE(ShellMenuWorker::DestroyMenuAndBitmaps(hMenu));
    EXPECT_FALSE(IsMenu(hMenu));
    EXPECT_FALSE(DeleteObject(sharedBmp));
}

// -----------------------------------------------------------------------------
// 12b. Submenu HMENU Destruction in removeExcludedMenuItems
// -----------------------------------------------------------------------------
TEST_F(ShellMenuWorkerTest, RemoveExcludedMenuItems_DestroysSubmenusAndDoesNotLeakHmenu)
{
    ShellMenuWorker worker;
    ASSERT_TRUE(worker.startWorker());

    // Construct test menu: Root -> NormalItem, Root -> ExcludedSubmenuItem (opens hSub)
    HMENU hRoot = CreatePopupMenu();
    ASSERT_NE(hRoot, nullptr);
    AppendMenuW(hRoot, MF_STRING, 101, L"NormalItem");

    HMENU hSub = CreatePopupMenu();
    ASSERT_NE(hSub, nullptr);
    AppendMenuW(hSub, MF_STRING, 201, L"ChildItem1");
    AppendMenuW(hSub, MF_STRING, 202, L"ChildItem2");
    AppendMenuW(hRoot, MF_POPUP, reinterpret_cast<UINT_PTR>(hSub), L"ExcludedSubmenuItem");

    ASSERT_TRUE(IsMenu(hSub));
    ASSERT_EQ(GetMenuItemCount(hRoot), 2);

    ShellMenuWorker::removeExcludedMenuItems(hRoot, { L"ExcludedSubmenu" });

    EXPECT_EQ(GetMenuItemCount(hRoot), 1);
    EXPECT_FALSE(IsMenu(hSub)); // Submenu handle must be destroyed, not orphaned

    ShellMenuWorker::DestroyMenuAndBitmaps(hRoot);
    worker.stopWorker();
}




// -----------------------------------------------------------------------------
// 12c. Verification: cancelRequest notifies callback with executed=false and emits menuDismissed
// -----------------------------------------------------------------------------
TEST_F(ShellMenuWorkerTest, CancelRequest_NotifiesCallbackAndEmitsMenuDismissed)
{
    ShellMenuWorker worker;
    ASSERT_TRUE(worker.startWorker());

    // Block the worker queue temporarily with a sleeping task
    std::promise<void> holdPromise;
    std::atomic<bool> holdReleased{ false };
    worker.postTask([&]() {
        while (!holdReleased.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    std::promise<bool> callbackFiredPromise;
    auto callbackFiredFuture = callbackFiredPromise.get_future();
    std::atomic<bool> callbackFired{ false };
    std::atomic<bool> resultExecuted{ true };

    ShellMenuRequest req;
    req.requestId = 65001;
    req.targetType = ShellTargetType::DesktopBackground;

    worker.postRequest(req, [&](const ShellMenuResult& res) {
        callbackFired = true;
        resultExecuted = res.executed;
        callbackFiredPromise.set_value(true);
    });

    // While held in queue, cancel the request
    worker.cancelRequest(65001);

    // Release the worker thread
    holdReleased.store(true);

    // Verify callback fires promptly with executed = false
    auto status = callbackFiredFuture.wait_for(std::chrono::milliseconds(2000));

    EXPECT_EQ(status, std::future_status::ready);
    EXPECT_TRUE(callbackFired.load());
    EXPECT_FALSE(resultExecuted.load());

    worker.stopWorker();
}






// =============================================================================
// Milestone 1 Challenger 2: Boundary Conditions, Fault Injection & STA Validity
// =============================================================================

namespace ShellMenuSeh {
HRESULT SafeQueryContextMenu(IContextMenu* pContextMenu, HMENU hMenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags);
HRESULT SafeInvokeCommand(IContextMenu* pContextMenu, CMINVOKECOMMANDINFO* pCmi);
HRESULT SafeHandleMenuMsg2(IContextMenu3* pContextMenu3, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT* plResult);
HRESULT SafeHandleMenuMsg(IContextMenu2* pContextMenu2, UINT uMsg, WPARAM wParam, LPARAM lParam);
HRESULT SafeGetCommandString(IContextMenu* pContextMenu, UINT_PTR idCmd, UINT uType, UINT* pReserved, LPSTR pszName, UINT cchMax);
}

class MockFaultInjectionContextMenu : public IContextMenu3 {
public:
    enum FaultMode {
        RaiseExceptionAv,
        HardwareNullDeref,
        NormalReturn
    };

    explicit MockFaultInjectionContextMenu(FaultMode mode = RaiseExceptionAv)
        : m_mode(mode) {}

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IContextMenu) {
            *ppv = static_cast<IContextMenu*>(this);
            AddRef();
            return S_OK;
        } else if (riid == IID_IContextMenu2) {
            *ppv = static_cast<IContextMenu2*>(this);
            AddRef();
            return S_OK;
        } else if (riid == IID_IContextMenu3) {
            *ppv = static_cast<IContextMenu3*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++m_ref; }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = --m_ref;
        if (r == 0) delete this;
        return r;
    }

    void triggerFault() {
        if (m_mode == RaiseExceptionAv) {
            RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        } else if (m_mode == HardwareNullDeref) {
            *reinterpret_cast<volatile int*>(0) = 42;
        }
    }

    // IContextMenu
    STDMETHODIMP QueryContextMenu(HMENU, UINT, UINT, UINT, UINT) override {
        triggerFault();
        return S_OK;
    }

    STDMETHODIMP InvokeCommand(LPCMINVOKECOMMANDINFO) override {
        triggerFault();
        return S_OK;
    }

    STDMETHODIMP GetCommandString(UINT_PTR, UINT, UINT*, LPSTR, UINT) override {
        triggerFault();
        return S_OK;
    }

    // IContextMenu2
    STDMETHODIMP HandleMenuMsg(UINT, WPARAM, LPARAM) override {
        triggerFault();
        return S_OK;
    }

    // IContextMenu3
    STDMETHODIMP HandleMenuMsg2(UINT, WPARAM, LPARAM, LRESULT*) override {
        triggerFault();
        return S_OK;
    }

private:
    FaultMode m_mode = RaiseExceptionAv;
    std::atomic<ULONG> m_ref{ 1 };
};

// -----------------------------------------------------------------------------
// 13. Fault Injection: Leaf SEH catches access violations without crashing
// -----------------------------------------------------------------------------
TEST_F(ShellMenuWorkerTest, FaultInjection_SEH_SafeFunctionsCatchAccessViolations)
{
    // 1. Verify null pointer defense
    EXPECT_EQ(ShellMenuSeh::SafeQueryContextMenu(nullptr, nullptr, 0, 1, 100, 0), E_POINTER);
    EXPECT_EQ(ShellMenuSeh::SafeInvokeCommand(nullptr, nullptr), E_POINTER);
    EXPECT_EQ(ShellMenuSeh::SafeHandleMenuMsg(nullptr, 0, 0, 0), E_POINTER);
    EXPECT_EQ(ShellMenuSeh::SafeHandleMenuMsg2(nullptr, 0, 0, 0, nullptr), E_POINTER);
    EXPECT_EQ(ShellMenuSeh::SafeGetCommandString(nullptr, 0, 0, nullptr, nullptr, 0), E_POINTER);

    // 2. Verify RaiseException(EXCEPTION_ACCESS_VIOLATION) fault injection
    {
        wil::com_ptr_nothrow<MockFaultInjectionContextMenu> mockAv = new MockFaultInjectionContextMenu(MockFaultInjectionContextMenu::RaiseExceptionAv);
        ASSERT_NE(mockAv.get(), nullptr);

        HMENU testMenu = CreatePopupMenu();
        EXPECT_EQ(ShellMenuSeh::SafeQueryContextMenu(mockAv.get(), testMenu, 0, 1, 100, 0), E_FAIL);
        DestroyMenu(testMenu);

        CMINVOKECOMMANDINFO cmi = {};
        cmi.cbSize = sizeof(cmi);
        EXPECT_EQ(ShellMenuSeh::SafeInvokeCommand(mockAv.get(), &cmi), E_FAIL);

        char buf[64] = "";
        EXPECT_EQ(ShellMenuSeh::SafeGetCommandString(mockAv.get(), 1, 0, nullptr, buf, sizeof(buf)), E_FAIL);

        EXPECT_EQ(ShellMenuSeh::SafeHandleMenuMsg(mockAv.get(), WM_INITMENUPOPUP, 0, 0), E_FAIL);

        LRESULT lres = 0;
        EXPECT_EQ(ShellMenuSeh::SafeHandleMenuMsg2(mockAv.get(), WM_INITMENUPOPUP, 0, 0, &lres), E_FAIL);
    }

    // 3. Verify Hardware Null Dereference fault injection
    {
        wil::com_ptr_nothrow<MockFaultInjectionContextMenu> mockHw = new MockFaultInjectionContextMenu(MockFaultInjectionContextMenu::HardwareNullDeref);
        ASSERT_NE(mockHw.get(), nullptr);

        HMENU testMenu = CreatePopupMenu();
        EXPECT_EQ(ShellMenuSeh::SafeQueryContextMenu(mockHw.get(), testMenu, 0, 1, 100, 0), E_FAIL);
        DestroyMenu(testMenu);

        CMINVOKECOMMANDINFO cmi = {};
        cmi.cbSize = sizeof(cmi);
        EXPECT_EQ(ShellMenuSeh::SafeInvokeCommand(mockHw.get(), &cmi), E_FAIL);

        char buf[64] = "";
        EXPECT_EQ(ShellMenuSeh::SafeGetCommandString(mockHw.get(), 1, 0, nullptr, buf, sizeof(buf)), E_FAIL);

        EXPECT_EQ(ShellMenuSeh::SafeHandleMenuMsg(mockHw.get(), WM_INITMENUPOPUP, 0, 0), E_FAIL);

        LRESULT lres = 0;
        EXPECT_EQ(ShellMenuSeh::SafeHandleMenuMsg2(mockHw.get(), WM_INITMENUPOPUP, 0, 0, &lres), E_FAIL);
    }
}

// -----------------------------------------------------------------------------
// 14. STA Apartment Validity: strictly maintained across 50 consecutive tasks
// -----------------------------------------------------------------------------
TEST_F(ShellMenuWorkerTest, STA_ApartmentValidityAcrossRepeatedTasks)
{
    ShellMenuWorker worker;
    ASSERT_TRUE(worker.startWorker());

    constexpr int TASK_COUNT = 50;
    DWORD mainThreadId = GetCurrentThreadId();
    std::atomic<int> staSuccessCount{ 0 };
    std::atomic<int> threadMatchCount{ 0 };

    std::promise<void> allDone;
    auto future = allDone.get_future();

    for (int i = 0; i < TASK_COUNT; ++i) {
        worker.postTask([&, i]() {
            APTTYPE apt = APTTYPE_CURRENT;
            APTTYPEQUALIFIER qual = APTTYPEQUALIFIER_NONE;
            HRESULT hr = CoGetApartmentType(&apt, &qual);
            if (SUCCEEDED(hr) && (apt == APTTYPE_STA || apt == APTTYPE_MAINSTA)) {
                staSuccessCount++;
            }

            DWORD tid = GetCurrentThreadId();
            if (tid == worker.workerThreadId() && tid != mainThreadId) {
                threadMatchCount++;
            }

            if (i == TASK_COUNT - 1) {
                allDone.set_value();
            }
        });
    }

    ASSERT_EQ(future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_EQ(staSuccessCount.load(), TASK_COUNT);
    EXPECT_EQ(threadMatchCount.load(), TASK_COUNT);

    worker.stopWorker();
}




// -----------------------------------------------------------------------------
// 15. Boundary: Invalid, empty, and non-existent targets
// -----------------------------------------------------------------------------
TEST_F(ShellMenuWorkerTest, Boundary_InvalidAndExtremeTargetPaths)
{
    ShellMenuWorker worker;
    ASSERT_TRUE(worker.startWorker());

    std::vector<ShellMenuRequest> boundaryRequests;

    // 1. Empty target paths across types
    for (auto type : { ShellTargetType::File, ShellTargetType::Folder, ShellTargetType::Shortcut, ShellTargetType::DiskLetter, ShellTargetType::BoxBackground }) {
        ShellMenuRequest r;
        r.requestId = 70000 + static_cast<int>(type);
        r.targetType = type;
        r.targetPath = "";
        boundaryRequests.push_back(r);
    }

    // 2. Non-existent path on valid drive
    {
        ShellMenuRequest r;
        r.requestId = 70010;
        r.targetType = ShellTargetType::File;
        r.targetPath = "C:\\LinDesk_NonExistent_FakePath_12345\\Dummy.txt";
        boundaryRequests.push_back(r);
    }

    // 3. Non-existent drive letter
    {
        ShellMenuRequest r;
        r.requestId = 70011;
        r.targetType = ShellTargetType::File;
        r.targetPath = "Z:\\NonExistent_Drive_Letter\\File.txt";
        boundaryRequests.push_back(r);
    }

    // 4. Unknown & out-of-range SpecialFolder
    {
        ShellMenuRequest r;
        r.requestId = 70012;
        r.targetType = ShellTargetType::SpecialFolder;
        r.specialFolder = SpecialFolders::UnknownFolder;
        boundaryRequests.push_back(r);
    }
    {
        ShellMenuRequest r;
        r.requestId = 70013;
        r.targetType = ShellTargetType::SpecialFolder;
        r.specialFolder = static_cast<SpecialFolders>(9999);
        boundaryRequests.push_back(r);
    }

    // 5. MultiObject with empty paths
    {
        ShellMenuRequest r;
        r.requestId = 70014;
        r.targetType = ShellTargetType::MultiObject;
        r.multiPaths = {};
        boundaryRequests.push_back(r);
    }

    // 6. MultiObject with multiple non-existent files
    {
        ShellMenuRequest r;
        r.requestId = 70015;
        r.targetType = ShellTargetType::MultiObject;
        r.multiPaths = { "C:\\no_file_1.txt", "D:\\no_file_2.txt", "Z:\\no_file_3.txt" };
        boundaryRequests.push_back(r);
    }

    std::atomic<int> completedCount{ 0 };
    std::promise<void> allDone;
    auto future = allDone.get_future();
    const int total = static_cast<int>(boundaryRequests.size());

    for (const auto& req : boundaryRequests) {
        worker.postRequest(req, [&, total](const ShellMenuResult& res) {
            EXPECT_FALSE(res.executed);
            if (++completedCount == total) {
                allDone.set_value();
            }
        });
    }

    ASSERT_EQ(future.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_EQ(completedCount.load(), total);

    worker.stopWorker();
}




// -----------------------------------------------------------------------------
// 16. Boundary: Overlong paths exceeding MAX_PATH (>260 chars)
// -----------------------------------------------------------------------------
TEST_F(ShellMenuWorkerTest, Boundary_LongPathsExceedingMaxPath)
{
    ShellMenuWorker worker;
    ASSERT_TRUE(worker.startWorker());

    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    QString deepPath = tempDir.path();
    for (int i = 0; i < 10; ++i) {
        deepPath += QString("/SubDir_Level_%1").arg(i);
    }
    QDir().mkpath(deepPath);

    QString longFileName = QString("LongFileName_TargetBoundaryTest_%1.txt").arg(QString("X").repeated(120));
    QString longFilePath = deepPath + "/" + longFileName;
    QString extendedPath = "\\\\?\\" + QDir::toNativeSeparators(longFilePath);

    QFile file(longFilePath);
    if (!file.open(QIODevice::WriteOnly)) {
        // Fallback to extended path syntax
        HANDLE hFile = CreateFileW(reinterpret_cast<LPCWSTR>(extendedPath.utf16()), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        ASSERT_NE(hFile, INVALID_HANDLE_VALUE);
        CloseHandle(hFile);
    } else {
        file.write("Long path payload content");
        file.close();
    }

    ASSERT_GT(longFilePath.length(), 260);

    // Test with standard path syntax
    {
        std::promise<bool> p;
        auto f = p.get_future();
        ShellMenuRequest r;
        r.requestId = 71001;
        r.targetType = ShellTargetType::File;
        r.targetPath = longFilePath;
        r.showMenu = false;

        worker.postRequest(r, [&](const ShellMenuResult& res) {
            p.set_value(res.executed);
        });

        ASSERT_EQ(f.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    }

    // Test with extended path syntax (\\?\)
    {
        std::promise<bool> p;
        auto f = p.get_future();
        ShellMenuRequest r;
        r.requestId = 71002;
        r.targetType = ShellTargetType::File;
        r.targetPath = extendedPath;
        r.showMenu = false;

        worker.postRequest(r, [&](const ShellMenuResult& res) {
            p.set_value(res.executed);
        });

        ASSERT_EQ(f.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    }

    worker.stopWorker();
}




// -----------------------------------------------------------------------------
// 17. Boundary: Unicode, CJK, whitespace, and special characters
// -----------------------------------------------------------------------------
TEST_F(ShellMenuWorkerTest, Boundary_UnicodeCJKAndSpecialCharacters)
{
    ShellMenuWorker worker;
    ASSERT_TRUE(worker.startWorker());

    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    QString complexName = "测试_日本語_한국어_#1 [v2]!@$%^&()_+'~` 文档.txt";
    QString complexPath = tempDir.filePath(complexName);

    QFile file(complexPath);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("UTF-8 complex filename payload");
    file.close();
    ASSERT_TRUE(QFile::exists(complexPath));

    std::promise<bool> p;
    auto f = p.get_future();
    ShellMenuRequest r;
    r.requestId = 72001;
    r.targetType = ShellTargetType::File;
    r.targetPath = complexPath;
    r.showMenu = false;

    worker.postRequest(r, [&](const ShellMenuResult& res) {
        p.set_value(res.executed);
    });

    ASSERT_EQ(f.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_TRUE(f.get());

    worker.stopWorker();
}




// -----------------------------------------------------------------------------
// 18. Boundary: Host Window message processing under bogus/null parameters
// -----------------------------------------------------------------------------
TEST_F(ShellMenuWorkerTest, Boundary_WindowMessagesEdgeCases)
{
    ShellMenuWorker worker;
    ASSERT_TRUE(worker.startWorker());

    HWND hwnd = worker.hostHwnd();
    ASSERT_NE(hwnd, nullptr);
    ASSERT_TRUE(IsWindow(hwnd));

    // Send null and zeroed messages to ensure window proc handles them safely
    SendMessageW(hwnd, WM_INITMENUPOPUP, 0, 0);
    SendMessageW(hwnd, WM_DRAWITEM, 0, 0);
    SendMessageW(hwnd, WM_MEASUREITEM, 0, 0);
    SendMessageW(hwnd, WM_MENUCHAR, 0, 0);

    // Verify window is still responsive
    LRESULT ping = SendMessageW(hwnd, WM_USER + 102, 50, 50);
    EXPECT_EQ(ping, 100);

    worker.stopWorker();
}




// -----------------------------------------------------------------------------
// 19. Shutdown Queue Drain: Exits cleanly in <500ms under 60 pending requests
// -----------------------------------------------------------------------------
TEST_F(ShellMenuWorkerTest, Shutdown_DrainsPendingQueueQuicklyWithoutTerminateOrHwndLeak)
{
    ShellMenuWorker worker;
    ASSERT_TRUE(worker.startWorker());
    HWND hwnd = worker.hostHwnd();
    ASSERT_NE(hwnd, nullptr);
    ASSERT_TRUE(IsWindow(hwnd));

    constexpr int QUEUE_SIZE = 60;
    std::atomic<int> callbacksInvoked{ 0 };

    for (int i = 0; i < QUEUE_SIZE; ++i) {
        ShellMenuRequest req;
        req.requestId = 85000 + i;
        req.targetType = ShellTargetType::DesktopBackground;
        req.showMenu = false;
        worker.postRequest(req, [&](const ShellMenuResult& res) {
            EXPECT_FALSE(res.executed);
            callbacksInvoked++;
        });
    }

    auto start = std::chrono::steady_clock::now();
    worker.stopWorker();
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    std::cout << "[DIAGNOSTIC] Shutdown_DrainsPendingQueueQuickly: elapsed=" << elapsedMs
              << " ms, callbacksInvoked=" << callbacksInvoked.load() << " / " << QUEUE_SIZE
              << " IsWindow=" << (IsWindow(hwnd) ? "TRUE (LEAKED)" : "FALSE (CLEAN)") << std::endl;

    // Must finish gracefully in <500ms (far below the 3000ms timeout that triggers terminate())
    EXPECT_LT(elapsedMs, 500);
    EXPECT_FALSE(worker.isWorkerRunning());
    EXPECT_FALSE(IsWindow(hwnd));
    EXPECT_EQ(callbacksInvoked.load(), QUEUE_SIZE);
}

// -----------------------------------------------------------------------------
// 20. Cancellation: Cancels item already swapped into local processing batch
// -----------------------------------------------------------------------------
TEST_F(ShellMenuWorkerTest, CancelRequest_CancelsItemInSwappedBatch)
{
    ShellMenuWorker worker;
    ASSERT_TRUE(worker.startWorker());

    constexpr int BATCH_SIZE = 12;
    std::atomic<int> completedCount{ 0 };
    std::atomic<bool> targetCancelledFired{ false };
    std::atomic<bool> targetExecutedState{ true };

    uint64_t targetId = 92006;

    for (int i = 0; i < BATCH_SIZE; ++i) {
        ShellMenuRequest req;
        req.requestId = 92000 + i;
        req.targetType = ShellTargetType::DesktopBackground;
        req.showMenu = false;

        worker.postRequest(req, [&, id = req.requestId](const ShellMenuResult& res) {
            if (id == targetId) {
                targetCancelledFired = true;
                targetExecutedState = res.executed;
            }
            completedCount++;
        });
    }

    // Cancel target request
    worker.cancelRequest(targetId);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (completedCount.load() < BATCH_SIZE && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    EXPECT_EQ(completedCount.load(), BATCH_SIZE);
    EXPECT_TRUE(targetCancelledFired.load());
    EXPECT_FALSE(targetExecutedState.load());

    worker.stopWorker();
}




// =============================================================================
// Milestone 1 Iteration 2 Challenger 2: Adversarial Modal Cancellation & Fault Injection
// =============================================================================

// 21. Challenge: Asynchronous pre-tracking / tracking cancellation across 15 randomized timings
TEST_F(ShellMenuWorkerTest, Challenge_RandomizedPreTrackingAndTrackingCancellationBurst)
{
    ShellMenuWorker worker;
    ASSERT_TRUE(worker.startWorker());

    constexpr int ITERATIONS = 15;
    for (int i = 0; i < ITERATIONS; ++i) {
        std::promise<bool> dismissedPromise;
        auto dismissedFuture = dismissedPromise.get_future();

        ShellMenuRequest req;
        req.requestId = 95000 + i;
        req.targetType = ShellTargetType::DesktopBackground;
        req.showMenu = true;
        req.globalPos = QPoint(100 + (i % 5) * 50, 100 + (i % 5) * 50);

        worker.postRequest(req, [&](const ShellMenuResult& res) {
            EXPECT_EQ(res.requestId, 95000ULL + i);
            EXPECT_FALSE(res.executed);
            dismissedPromise.set_value(true);
        });

        // Delay varied between 0ms and 45ms to hit all phases:
        // bind, SafeQueryContextMenu, between query and TrackPopupMenu, and active tracking.
        int delayMs = (i * 7) % 45;
        if (delayMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        }
        worker.cancelActiveMenu();

        ASSERT_EQ(dismissedFuture.wait_for(std::chrono::seconds(5)), std::future_status::ready)
            << "Timed out at iteration " << i << " with delayMs=" << delayMs;
        EXPECT_TRUE(dismissedFuture.get());
        EXPECT_FALSE(worker.isTrackingMenu());
    }

    worker.stopWorker();
}




// 22. Challenge: Direct SendMessage WM_CANCELMODE to host HWND during active tracking
TEST_F(ShellMenuWorkerTest, Challenge_DirectSendMessageCancelModeToHostWindow)
{
    ShellMenuWorker worker;
    ASSERT_TRUE(worker.startWorker());

    std::promise<bool> dismissedPromise;
    auto dismissedFuture = dismissedPromise.get_future();

    ShellMenuRequest req;
    req.requestId = 96001;
    req.targetType = ShellTargetType::DesktopBackground;
    req.showMenu = true;
    req.globalPos = QPoint(120, 120);

    std::thread sender([&]() {
        for (int i = 0; i < 100; ++i) {
            if (worker.isTrackingMenu()) {
                // Synchronously send WM_CANCELMODE from external thread to worker HWND
                SendMessageW(worker.hostHwnd(), WM_CANCELMODE, 0, 0);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        SendMessageW(worker.hostHwnd(), WM_CANCELMODE, 0, 0);
    });

    worker.postRequest(req, [&](const ShellMenuResult& res) {
        EXPECT_EQ(res.requestId, 96001ULL);
        EXPECT_FALSE(res.executed);
        dismissedPromise.set_value(true);
    });

    ASSERT_EQ(dismissedFuture.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_TRUE(dismissedFuture.get());
    EXPECT_FALSE(worker.isTrackingMenu());

    sender.join();
    worker.stopWorker();
}




// 23. Challenge: Stop worker while modal menu is actively tracking
TEST_F(ShellMenuWorkerTest, Challenge_StopWorkerWhileModalMenuIsTracking)
{
    ShellMenuWorker worker;
    ASSERT_TRUE(worker.startWorker());
    HWND hwnd = worker.hostHwnd();
    ASSERT_NE(hwnd, nullptr);

    std::promise<bool> callbackPromise;
    auto callbackFuture = callbackPromise.get_future();

    ShellMenuRequest req;
    req.requestId = 97001;
    req.targetType = ShellTargetType::DesktopBackground;
    req.showMenu = true;
    req.globalPos = QPoint(150, 150);

    worker.postRequest(req, [&](const ShellMenuResult& res) {
        EXPECT_EQ(res.requestId, 97001ULL);
        EXPECT_FALSE(res.executed);
        callbackPromise.set_value(true);
    });

    // Wait until menu is actively tracking
    for (int i = 0; i < 50; ++i) {
        if (worker.isTrackingMenu()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto startStop = std::chrono::steady_clock::now();
    worker.stopWorker();
    auto stopMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startStop).count();

    // Must cleanly stop without 3-second hang or TerminateThread
    EXPECT_LT(stopMs, 1000);
    EXPECT_FALSE(worker.isWorkerRunning());
    EXPECT_FALSE(IsWindow(hwnd));

    ASSERT_EQ(callbackFuture.wait_for(std::chrono::milliseconds(500)), std::future_status::ready);
    EXPECT_TRUE(callbackFuture.get());
}

// 24. Challenge: Fault injection — Non-existent, redundant, and repeated cancelRequest calls
TEST_F(ShellMenuWorkerTest, Challenge_CancelRequest_FaultInjectionAndRedundancy)
{
    ShellMenuWorker worker;
    ASSERT_TRUE(worker.startWorker());

    // 1. Cancel request that was never posted
    worker.cancelRequest(99999999ULL);
    worker.cancelRequest(0ULL);

    // 2. Post valid request and let it complete
    std::promise<bool> completePromise;
    auto completeFuture = completePromise.get_future();

    ShellMenuRequest req;
    req.requestId = 98001;
    req.targetType = ShellTargetType::DesktopBackground;
    req.showMenu = false;

    worker.postRequest(req, [&](const ShellMenuResult& res) {
        EXPECT_TRUE(res.executed);
        completePromise.set_value(true);
    });

    ASSERT_EQ(completeFuture.wait_for(std::chrono::seconds(5)), std::future_status::ready);

    // 3. Cancel request after it already completed
    worker.cancelRequest(98001);
    worker.cancelRequest(98001);

    // 4. Verify worker queue is completely healthy by executing a task
    std::promise<int> verifyPromise;
    auto verifyFuture = verifyPromise.get_future();
    worker.postTask([&]() {
        verifyPromise.set_value(12345);
    });

    ASSERT_EQ(verifyFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_EQ(verifyFuture.get(), 12345);

    worker.stopWorker();
}




// 25. Challenge: Fault injection — Malformed messages to host window
TEST_F(ShellMenuWorkerTest, Challenge_FaultInjection_MalformedWindowMessages)
{
    ShellMenuWorker worker;
    ASSERT_TRUE(worker.startWorker());
    HWND hwnd = worker.hostHwnd();
    ASSERT_NE(hwnd, nullptr);

    // 1. Malformed DRAWITEMSTRUCT with garbage lParam
    SendMessageW(hwnd, WM_DRAWITEM, 0, 0);
    DRAWITEMSTRUCT corruptDis = {};
    corruptDis.CtlType = ODT_MENU;
    corruptDis.CtlID = 99999;
    SendMessageW(hwnd, WM_DRAWITEM, 0, reinterpret_cast<LPARAM>(&corruptDis));

    // 2. Malformed MEASUREITEMSTRUCT
    SendMessageW(hwnd, WM_MEASUREITEM, 0, 0);
    MEASUREITEMSTRUCT corruptMis = {};
    corruptMis.CtlType = ODT_MENU;
    corruptMis.CtlID = 99999;
    SendMessageW(hwnd, WM_MEASUREITEM, 0, reinterpret_cast<LPARAM>(&corruptMis));

    // 3. Null HMENU on WM_INITMENUPOPUP
    SendMessageW(hwnd, WM_INITMENUPOPUP, 0, 0);

    // 4. Cancel messages when idle
    SendMessageW(hwnd, WM_CANCELMODE, 0, 0);
    SendMessageW(hwnd, ShellMenuWorker::WM_SHELL_CANCEL_ACTIVE, 0, 0);

    // 5. Unknown arbitrary messages
    SendMessageW(hwnd, WM_APP + 999, 123, 456);

    // Verify host window is still completely functional
    LRESULT ping = SendMessageW(hwnd, WM_USER + 102, 33, 44);
    EXPECT_EQ(ping, 77);

    worker.stopWorker();
}




// 26. Challenge: Active cancellation followed immediately by next request execution
TEST_F(ShellMenuWorkerTest, Challenge_ActiveCancellationFollowedByNextRequest)
{
    ShellMenuWorker worker;
    ASSERT_TRUE(worker.startWorker());

    constexpr int CYCLES = 5;
    for (int rep = 0; rep < CYCLES; ++rep) {
        std::promise<bool> req1DismissedPromise;
        auto req1DismissedFuture = req1DismissedPromise.get_future();

        std::promise<bool> req2CompletedPromise;
        auto req2CompletedFuture = req2CompletedPromise.get_future();

        ShellMenuRequest req1;
        req1.requestId = 99000ULL + rep * 2 + 1;
        req1.targetType = ShellTargetType::DesktopBackground;
        req1.showMenu = true;
        req1.globalPos = QPoint(100, 100);

        ShellMenuRequest req2;
        req2.requestId = 99000ULL + rep * 2 + 2;
        req2.targetType = ShellTargetType::DesktopBackground;
        req2.showMenu = false;

        worker.postRequest(req1, [&](const ShellMenuResult& res) {
            EXPECT_EQ(res.requestId, req1.requestId);
            EXPECT_FALSE(res.executed);
            req1DismissedPromise.set_value(true);
        });

        // Wait until req1 is tracking, then cancel it
        for (int i = 0; i < 50; ++i) {
            if (worker.isTrackingMenu()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        worker.cancelActiveMenu();

        ASSERT_EQ(req1DismissedFuture.wait_for(std::chrono::seconds(5)), std::future_status::ready);
        EXPECT_TRUE(req1DismissedFuture.get());
        EXPECT_FALSE(worker.isTrackingMenu());

        // Immediately follow with next request on the same worker
        worker.postRequest(req2, [&](const ShellMenuResult& res) {
            EXPECT_EQ(res.requestId, req2.requestId);
            EXPECT_TRUE(res.executed);
            req2CompletedPromise.set_value(true);
        });

        ASSERT_EQ(req2CompletedFuture.wait_for(std::chrono::seconds(5)), std::future_status::ready);
        EXPECT_TRUE(req2CompletedFuture.get());
    }

    worker.stopWorker();
}




// -----------------------------------------------------------------------------
// 27. Defect Reproduction: Reentrant queue processing during modal tracking
//     corrupts active request tracking and causes cancelRequest to hang.
// -----------------------------------------------------------------------------
TEST_F(ShellMenuWorkerTest, Defect_ReentrantQueueProcessingDuringModalTrackingCorruptsActiveRequestState)
{
    ShellMenuWorker worker;
    ASSERT_TRUE(worker.startWorker());

    std::promise<bool> req1DismissedPromise;
    auto req1DismissedFuture = req1DismissedPromise.get_future();

    std::promise<bool> req2CompletedPromise;
    auto req2CompletedFuture = req2CompletedPromise.get_future();

    ShellMenuRequest req1;
    req1.requestId = 88001;
    req1.targetType = ShellTargetType::DesktopBackground;
    req1.showMenu = true;
    req1.globalPos = QPoint(100, 100);

    ShellMenuRequest req2;
    req2.requestId = 88002;
    req2.targetType = ShellTargetType::DesktopBackground;
    req2.showMenu = false;

    std::atomic<bool> req1CallbackFired{ false };
    worker.postRequest(req1, [&](const ShellMenuResult& res) {
        EXPECT_EQ(res.requestId, 88001ULL);
        EXPECT_FALSE(res.executed);
        req1CallbackFired = true;
        req1DismissedPromise.set_value(true);
    });

    // 1. Wait until req1 is actively tracking modal menu
    for (int i = 0; i < 100; ++i) {
        if (worker.isTrackingMenu()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(worker.isTrackingMenu());

    // 2. While req1 is actively tracking, post req2 (or postTask)
    worker.postRequest(req2, [&](const ShellMenuResult& res) {
        EXPECT_EQ(res.requestId, 88002ULL);
        EXPECT_TRUE(res.executed);
        req2CompletedPromise.set_value(true);
    });

    // 3. Wait for req2 to complete (which happens reentrantly inside TrackPopupMenu)
    ASSERT_EQ(req2CompletedFuture.wait_for(std::chrono::seconds(3)), std::future_status::ready);
    EXPECT_TRUE(req2CompletedFuture.get());

    // req1 is still on screen (TrackPopupMenu hasn't returned)
    EXPECT_TRUE(worker.isTrackingMenu());

    // 4. Now attempt to cancel req1 via cancelRequest(88001)
    worker.cancelRequest(88001);

    // 5. Verify that req1 is dismissed promptly within 3 seconds
    auto status = req1DismissedFuture.wait_for(std::chrono::seconds(3));
    std::cout << "[DEFECT TEST] req1 wait status: "
              << (status == std::future_status::ready ? "READY" : "TIMEOUT (STALLED)")
              << " isTrackingMenu=" << worker.isTrackingMenu()
              << " req1CallbackFired=" << req1CallbackFired.load() << std::endl;

    EXPECT_EQ(status, std::future_status::ready);
    EXPECT_TRUE(req1CallbackFired.load());
    EXPECT_FALSE(worker.isTrackingMenu());

    worker.stopWorker();
}




// -----------------------------------------------------------------------------
// Challenge: Pre-tracking cancellation intercepted by WM_ENTERMENULOOP
// -----------------------------------------------------------------------------
TEST_F(ShellMenuWorkerTest, Challenge_PreTrackingCancellation_InterceptedByWmEnterMenuLoop)
{
    ShellMenuWorker worker;
    ASSERT_TRUE(worker.startWorker());

    std::promise<bool> dismissedPromise;
    auto dismissedFuture = dismissedPromise.get_future();

    ShellMenuRequest req;
    req.requestId = 77001;
    req.targetType = ShellTargetType::DesktopBackground;
    req.showMenu = true;
    req.globalPos = QPoint(150, 150);

    // Set cancellation pending BEFORE or as menu loop enters
    worker.postRequest(req, [&](const ShellMenuResult& res) {
        EXPECT_EQ(res.requestId, 77001ULL);
        EXPECT_FALSE(res.executed);
        dismissedPromise.set_value(true);
    });

    // Induce cancellation immediately in the microsecond pre-tracking window
    worker.cancelActiveMenu();

    // Must resolve promptly without hanging or waiting for user interaction
    ASSERT_EQ(dismissedFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_TRUE(dismissedFuture.get());
    EXPECT_FALSE(worker.isTrackingMenu());

    worker.stopWorker();
}

// -----------------------------------------------------------------------------
// Step-by-Step Lnk Investigation
// -----------------------------------------------------------------------------
using unique_pidl_absolute = wil::unique_cotaskmem_ptr<std::remove_pointer_t<PIDLIST_ABSOLUTE>>;

TEST_F(ShellMenuWorkerTest, StepByStep_Lnk_Investigation)
{
    const std::wstring lnkPath = L"D:\\Desktop\\火绒安全软件.lnk";
    if (GetFileAttributesW(lnkPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        GTEST_SKIP() << "Target lnk not found";
    }

    ShellMenuWorker worker;
    ASSERT_TRUE(worker.startWorker());

    std::promise<void> donePromise;
    auto doneFuture = donePromise.get_future();

    worker.postTask([&]() {
        QElapsedTimer timer;

        // Step 1: SHParseDisplayName
        timer.start();
        unique_pidl_absolute itemPidl;
        HRESULT hr = SHParseDisplayName(lnkPath.c_str(), nullptr, wil::out_param(itemPidl), 0, nullptr);
        std::cout << "[Step 1] SHParseDisplayName: " << timer.restart() << " ms (hr=" << std::hex << hr << ")\n";

        // Step 2: SHBindToParent
        LPCITEMIDLIST relativePidl = nullptr;
        wil::com_ptr_nothrow<IShellFolder> parentFolder;
        hr = SHBindToParent(itemPidl.get(), IID_PPV_ARGS(&parentFolder), &relativePidl);
        std::cout << "[Step 2] SHBindToParent: " << timer.restart() << " ms\n";

        // Step 3: GetUIObjectOf with nullptr vs worker.workerHwnd()
        wil::com_ptr_nothrow<IContextMenu> cmNull;
        hr = parentFolder->GetUIObjectOf(nullptr, 1, &relativePidl, IID_IContextMenu, nullptr, cmNull.put_void());
        std::cout << "[Step 3a] GetUIObjectOf(nullptr): " << timer.restart() << " ms\n";

        wil::com_ptr_nothrow<IContextMenu> cmHwnd;
        hr = parentFolder->GetUIObjectOf(worker.workerHwnd(), 1, &relativePidl, IID_IContextMenu, nullptr, cmHwnd.put_void());
        std::cout << "[Step 3b] GetUIObjectOf(workerHwnd): " << timer.restart() << " ms\n";

        // Step 4: Test QueryContextMenu with different flags
        auto benchmarkQuery = [&](const char* name, IContextMenu* cm, UINT flags, bool attachSite) {
            class TestSite : public IServiceProvider, public IOleWindow {
            public:
                HWND m_owner = nullptr;
                explicit TestSite(HWND h) : m_owner(h) {}
                STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
                    if (!ppv) return E_POINTER;
                    if (riid == IID_IUnknown || riid == IID_IServiceProvider) {
                        *ppv = static_cast<IServiceProvider*>(this); return S_OK;
                    }
                    if (riid == IID_IOleWindow) {
                        *ppv = static_cast<IOleWindow*>(this); return S_OK;
                    }
                    *ppv = nullptr; return E_NOINTERFACE;
                }
                STDMETHODIMP_(ULONG) AddRef() override { return 1; }
                STDMETHODIMP_(ULONG) Release() override { return 1; }
                STDMETHODIMP QueryService(REFGUID guidService, REFIID riid, void** ppv) override {
                    if (!ppv) return E_POINTER;
                    if (riid == IID_IOleWindow || guidService == SID_SShellBrowser) {
                        if (riid == IID_IOleWindow) {
                            *ppv = static_cast<IOleWindow*>(this); return S_OK;
                        }
                    }
                    *ppv = nullptr; return E_NOINTERFACE;
                }
                STDMETHODIMP GetWindow(HWND* ph) override {
                    if (!ph) return E_POINTER;
                    *ph = m_owner; return S_OK;
                }
                STDMETHODIMP ContextSensitiveHelp(BOOL) override { return E_NOTIMPL; }
            } site(worker.workerHwnd());

            wil::com_ptr_nothrow<IObjectWithSite> ows;
            if (attachSite && cm) {
                cm->QueryInterface(IID_PPV_ARGS(&ows));
                if (ows) ows->SetSite(static_cast<IServiceProvider*>(&site));
            }

            wil::unique_hmenu hMenu(CreatePopupMenu());
            timer.restart();
            HRESULT qhr = ShellMenuSeh::SafeQueryContextMenu(cm, hMenu.get(), 0, 1, 9999, flags);
            qint64 elapsed = timer.elapsed();
            int count = GetMenuItemCount(hMenu.get());
            if (ows) ows->SetSite(nullptr);

            std::cout << "  " << name << ": elapsed=" << std::dec << elapsed << " ms, items=" << count
                      << ", hr=" << std::hex << qhr << "\n";
        };

        std::cout << "\n--- Testing QueryContextMenu Flags on cmHwnd (with site) ---\n";
        benchmarkQuery("LinDesk Original: CMF_NORMAL|CMF_CANRENAME|CMF_ITEMMENU|CMF_ASYNCVERBSTATE",
            cmHwnd.get(), CMF_NORMAL | CMF_CANRENAME | CMF_ITEMMENU | CMF_ASYNCVERBSTATE, true);

        benchmarkQuery("Comment out CMF_ASYNCVERBSTATE (CMF_NORMAL|CMF_CANRENAME|CMF_ITEMMENU)",
            cmHwnd.get(), CMF_NORMAL | CMF_CANRENAME | CMF_ITEMMENU, true);

        benchmarkQuery("Comment out CMF_ITEMMENU (CMF_NORMAL|CMF_CANRENAME)",
            cmHwnd.get(), CMF_NORMAL | CMF_CANRENAME, true);

        benchmarkQuery("Only CMF_NORMAL",
            cmHwnd.get(), CMF_NORMAL, true);

        benchmarkQuery("CMF_NORMAL | CMF_EXPLORE",
            cmHwnd.get(), CMF_NORMAL | CMF_EXPLORE, true);

        benchmarkQuery("CMF_NORMAL | CMF_OPTIMIZEFORINVOKE",
            cmHwnd.get(), CMF_NORMAL | CMF_OPTIMIZEFORINVOKE, true);

        std::cout << "\n--- Testing QueryContextMenu Flags on cmNull (WITHOUT site) ---\n";
        benchmarkQuery("cmNull NO SITE: CMF_NORMAL|CMF_CANRENAME|CMF_ITEMMENU|CMF_ASYNCVERBSTATE",
            cmNull.get(), CMF_NORMAL | CMF_CANRENAME | CMF_ITEMMENU | CMF_ASYNCVERBSTATE, false);

        benchmarkQuery("cmNull NO SITE: CMF_NORMAL|CMF_CANRENAME",
            cmNull.get(), CMF_NORMAL | CMF_CANRENAME, false);

        benchmarkQuery("cmNull NO SITE: Only CMF_NORMAL",
            cmNull.get(), CMF_NORMAL, false);

        donePromise.set_value();
    });

    ASSERT_EQ(doneFuture.wait_for(std::chrono::seconds(10)), std::future_status::ready);
    worker.stopWorker();
}

TEST_F(ShellMenuWorkerTest, EndToEnd_Lnk_Timing)
{
    ShellMenuWorker worker;
    ASSERT_TRUE(worker.startWorker());

    std::promise<qint64> donePromise;
    auto doneFuture = donePromise.get_future();

    ShellMenuRequest req;
    req.requestId = 99991;
    req.targetType = ShellTargetType::Shortcut;
    req.targetPath = QStringLiteral("D:\\Desktop\\火绒安全软件.lnk");
    req.showMenu = false;

    QElapsedTimer timer;
    timer.start();
    worker.postRequest(req, [&](const ShellMenuResult& res) {
        donePromise.set_value(timer.elapsed());
    });

    ASSERT_EQ(doneFuture.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    qint64 ms = doneFuture.get();
    std::cout << "[EndToEnd_Lnk_Timing] Total worker elapsed: " << ms << " ms\n";
    worker.stopWorker();
}

