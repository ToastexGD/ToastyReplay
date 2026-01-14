#pragma once
#include <string>
#include <vector>
#include <functional>

namespace keybinds {
    struct Keybind {
        int key = 0;
        int mod = 0;
        static Keybind create(int k, int m) { return Keybind{k,m}; }
    };

    struct Modifier { enum Enum { None = 0 }; };

    struct Bindable {
        std::string id;
        std::string name;
        std::string desc;
        std::vector<Keybind> binds;
        std::string group;
    };

    class BindManager {
    public:
        static BindManager* get() { static BindManager m; return &m; }
        void registerBindable(const Bindable&) {}
    };

    struct InvokeBindEvent {
        bool isDown() const { return false; }
    };

    enum ListenerResult { Propagate, Stop };

    template<typename Filter>
    class EventListener {
    public:
        EventListener(std::function<ListenerResult(InvokeBindEvent*)> cb, Filter) {}
    };

    struct InvokeBindFilter {
        InvokeBindFilter(void*, const std::string&) {}
    };

}

inline std::string operator"" _spr(const char* s, size_t) { return std::string(s); }

// Key constants used in keybinds usage
#define KEY_B 66
#define KEY_C 67
#define KEY_V 86
