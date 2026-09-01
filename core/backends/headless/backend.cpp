#include <backend.h>
#include <utils/flog.h>

namespace backend {
    int init(std::string resDir) {
        flog::info("Headless backend initialized (WebUI mode active)");
        return 0;
    }

    void beginFrame() {}
    void render(bool vsync) {}

    void getMouseScreenPos(double& x, double& y) {
        x = 0;
        y = 0;
    }

    void setMouseScreenPos(double x, double y) {}

    int renderLoop() {
        return 0;
    }

    int end() {
        return 0;
    }
}
