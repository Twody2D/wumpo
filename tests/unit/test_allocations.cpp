#include "core/fixed_string.hpp"
#include "game/demo_scene.hpp"
#include "game/shift.hpp"
#include "input/button.hpp"
#include "input/input_state.hpp"
#include "renderer/framebuffer.hpp"

#include <doctest.h>

#include <cstddef>
#include <cstdlib>
#include <new>
#include <ostream>

/// Turns "do not allocate during gameplay" from a rule nobody can check into an
/// invariant the build enforces.
///
/// On the desktop an allocation per frame is invisible, which is exactly the
/// problem: the rule would rot silently and only be discovered when the runtime
/// meets a microcontroller with kilobytes of RAM and no heap worth the name.
///
/// Global operator new is replaced for the whole test binary. Counting is off by
/// default, so doctest's own allocations are ignored, and switched on only
/// around the code under test.
namespace {

std::size_t g_allocations = 0;
bool g_counting = false;

/// Counts allocations inside its scope.
class AllocationGuard {
public:
    AllocationGuard() {
        g_allocations = 0;
        g_counting = true;
    }

    AllocationGuard(const AllocationGuard&) = delete;
    AllocationGuard(AllocationGuard&&) = delete;
    AllocationGuard& operator=(const AllocationGuard&) = delete;
    AllocationGuard& operator=(AllocationGuard&&) = delete;

    ~AllocationGuard() { g_counting = false; }

    [[nodiscard]] static std::size_t count() noexcept { return g_allocations; }
};

} // namespace

void* operator new(std::size_t size) {
    if (g_counting) {
        ++g_allocations;
    }
    void* memory = std::malloc(size == 0 ? 1 : size);
    if (memory == nullptr) {
        throw std::bad_alloc();
    }
    return memory;
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* block) noexcept {
    std::free(block);
}
void operator delete[](void* block) noexcept {
    std::free(block);
}
void operator delete(void* block, std::size_t size) noexcept {
    (void)size;
    std::free(block);
}
void operator delete[](void* block, std::size_t size) noexcept {
    (void)size;
    std::free(block);
}

using wumpo::game::DemoScene;
using wumpo::game::ShiftGame;
using wumpo::input::Button;
using wumpo::input::ButtonMask;
using wumpo::input::InputState;
using wumpo::renderer::Framebuffer;
namespace input = wumpo::input;

TEST_SUITE("alloc") {

    TEST_CASE("the guard itself notices an allocation") {
        // A counter that never fires would make every test below vacuous.
        std::size_t counted = 0;
        {
            const AllocationGuard guard;
            volatile auto* leaked = new int(5); // NOLINT(cppcoreguidelines-owning-memory)
            counted = AllocationGuard::count();
            delete leaked; // NOLINT(cppcoreguidelines-owning-memory)
        }
        CHECK(counted == 1);
    }

    TEST_CASE("ten thousand ticks allocate nothing") {
        DemoScene scene(12345);
        InputState state;

        // Constructed outside the guard: setting a run up may allocate, playing it
        // may not.
        {
            const AllocationGuard guard;
            for (int tick = 0; tick < 10'000; ++tick) {
                ButtonMask mask = 0;
                if (tick % 5 < 2) {
                    mask = static_cast<ButtonMask>(mask | input::maskOf(Button::Right));
                }
                if (tick % 9 < 3) {
                    mask = static_cast<ButtonMask>(mask | input::maskOf(Button::Up));
                }
                if (tick % 31 == 0) {
                    mask = static_cast<ButtonMask>(mask | input::maskOf(Button::A));
                }
                state.update(mask);
                (void)scene.tick(state);
            }
            CHECK(AllocationGuard::count() == 0);
        }
    }

    TEST_CASE("rendering allocates nothing, including the game over screen") {
        // Drawing happens every frame forever. A single std::to_string here is an
        // allocation per frame, which is what this test exists to catch.
        DemoScene scene(1);
        Framebuffer frame;
        InputState state;

        {
            const AllocationGuard guard;
            scene.render(frame);
            CHECK(AllocationGuard::count() == 0);
        }

        // Run it out so the game over screen, with its formatted score, is drawn.
        for (int tick = 0; tick < DemoScene::kRunTicks; ++tick) {
            state.update(0);
            (void)scene.tick(state);
        }
        REQUIRE(scene.phase() == DemoScene::Phase::Over);

        {
            const AllocationGuard guard;
            scene.render(frame);
            CHECK(AllocationGuard::count() == 0);
        }
    }

    TEST_CASE("a restart allocates nothing") {
        // Restarting is the most common thing a player does, and reseeding a
        // generator or clearing state are all places a container could creep in.
        DemoScene scene(1);
        InputState state;
        for (int tick = 0; tick < DemoScene::kRunTicks; ++tick) {
            state.update(0);
            (void)scene.tick(state);
        }
        REQUIRE(scene.phase() == DemoScene::Phase::Over);

        {
            const AllocationGuard guard;
            state.update(input::maskOf(Button::A));
            (void)scene.tick(state);
            CHECK(AllocationGuard::count() == 0);
        }
        CHECK(scene.phase() == DemoScene::Phase::Playing);
    }

    TEST_CASE("building screen text allocates nothing") {
        const AllocationGuard guard;
        wumpo::core::ScreenText text("SCORE ");
        text.append(123456);
        CHECK(AllocationGuard::count() == 0);
        CHECK(text.view() == "SCORE 123456");
    }

    TEST_CASE("screen text truncates instead of growing") {
        wumpo::core::FixedString<4> text;
        text.append("ABCDEFGH");
        CHECK(text.view() == "ABCD");
        CHECK(text.truncated());
    }

    TEST_CASE("a whole frame of runtime work allocates nothing") {
        // The realistic loop: poll, tick, render. If any layer starts allocating,
        // this is where it shows up.
        DemoScene scene(7);
        InputState state;
        Framebuffer frame;

        {
            const AllocationGuard guard;
            for (int frame_index = 0; frame_index < 600; ++frame_index) {
                state.update(input::maskOf(Button::Right));
                (void)scene.tick(state);
                scene.render(frame);
            }
            CHECK(AllocationGuard::count() == 0);
        }
    }

    TEST_CASE("ten thousand ticks of The Shift allocate nothing") {
        ShiftGame game(12345);
        InputState state;

        {
            const AllocationGuard guard;
            for (int tick = 0; tick < 10'000; ++tick) {
                ButtonMask mask = 0;
                if (tick % 5 < 2) {
                    mask = static_cast<ButtonMask>(mask | input::maskOf(Button::Right));
                }
                if (tick % 9 < 3) {
                    mask = static_cast<ButtonMask>(mask | input::maskOf(Button::Left));
                }
                if (tick % 31 == 0) {
                    mask = static_cast<ButtonMask>(mask | input::maskOf(Button::A));
                }
                state.update(mask);
                (void)game.tick(state);
            }
            CHECK(AllocationGuard::count() == 0);
        }
    }

    TEST_CASE("rendering allocates nothing, including The Shift's crash screen") {
        ShiftGame game(12345);
        Framebuffer frame;

        {
            const AllocationGuard guard;
            game.render(frame);
            CHECK(AllocationGuard::count() == 0);
        }

        // Held hard left for longer than a wall takes to fall; seed 12345's
        // first gap sits away from the left edge, so this always crashes.
        InputState state;
        for (int tick = 0; tick < 200 && game.phase() == ShiftGame::Phase::Playing; ++tick) {
            state.update(input::maskOf(Button::Left));
            (void)game.tick(state);
        }
        REQUIRE(game.phase() == ShiftGame::Phase::Over);

        {
            const AllocationGuard guard;
            game.render(frame);
            CHECK(AllocationGuard::count() == 0);
        }
    }

    TEST_CASE("a restart allocates nothing in The Shift") {
        ShiftGame game(12345);
        InputState state;
        for (int tick = 0; tick < 200 && game.phase() == ShiftGame::Phase::Playing; ++tick) {
            state.update(input::maskOf(Button::Left));
            (void)game.tick(state);
        }
        REQUIRE(game.phase() == ShiftGame::Phase::Over);

        {
            const AllocationGuard guard;
            state.update(input::maskOf(Button::A));
            (void)game.tick(state);
            CHECK(AllocationGuard::count() == 0);
        }
        CHECK(game.phase() == ShiftGame::Phase::Playing);
    }

} // TEST_SUITE
