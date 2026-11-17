#pragma once

// 退出期间必须始终禁止折叠。截图完成消息和恢复重试共用同一个退出状态，
// 避免多个布尔开关不同步，导致退出等待期间又把图标隐藏回去。
struct AppLifecycle {
    enum class Phase { Running, Stopping, Destroying };
    Phase phase = Phase::Running;
    unsigned restoreAttempts = 0;

    bool CanFold() const {
        return phase == Phase::Running;
    }
    bool IsStopping() const {
        return phase == Phase::Stopping;
    }
    bool IsShuttingDown() const {
        return phase == Phase::Destroying;
    }
    void BeginExit() {
        phase = Phase::Stopping;
        restoreAttempts = 0;
    }
    void CancelExit() {
        phase = Phase::Running;
        restoreAttempts = 0;
    }
    void BeginShutdown() {
        phase = Phase::Destroying;
    }
};
