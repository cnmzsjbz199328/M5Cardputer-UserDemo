#pragma once

#include <mooncake.h>
#include <hal/keyboard/keyboard.h>
#include <string>
#include <vector>

class AppSdDiag : public mooncake::AppAbility {
public:
    AppSdDiag();
    ~AppSdDiag();
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    int _key_event_slot_id = -1;
    int _page = 0;
    uint32_t _last_probe_ms = 0;
    std::vector<std::string> _lines;
    void run_probe();
    void render();
    void handle_key(const Keyboard::KeyEvent_t& event);
};
