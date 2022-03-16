#pragma once

#include <map>
#include <array>

#include <donut/core/math/math.h>
#include <GLFW/glfw3.h>

namespace donut::app
{

    // A camera with position and orientation. Methods for moving it come from derived classes.
    class BaseCamera
    {
    public:
        virtual void KeyboardUpdate(int key, int scancode, int action, int mods) = 0;
        virtual void MousePosUpdate(double xpos, double ypos) = 0;
        virtual void MouseButtonUpdate(int button, int action, int mods) = 0;
        virtual void MouseScrollUpdate(double xoffset, double yoffset) = 0;
        virtual void Animate(float deltaT) = 0;

        void SetMoveSpeed(float moveSpeed);
        void SetRotateSpeed(float rotateSpeed);

        virtual const dm::affine3& GetWorldToViewMatrix();
        virtual const dm::affine3& GetTranslatedWorldToViewMatrix();
        const dm::float3& GetPosition();
        const dm::float3& GetDir();
        const dm::float3& GetUp();

    protected:
        BaseCamera();

        // This can be useful for derived classes while not necessarily public, i.e., in a third person
        // camera class, public clients cannot direct the gaze point.
        void BaseLookAt(dm::float3 posCamera, dm::float3 posTarget, dm::float3 up = dm::float3{ 0.f, 1.f, 0.f });
        void UpdateWorldToView();

        dm::affine3 matWorldToView;
        dm::affine3 matTranslatedWorldToView;

        dm::float3 cameraPos;   // in worldspace
        dm::float3 cameraDir;   // normalized
        dm::float3 cameraUp;    // normalized
        dm::float3 cameraRight; // normalized

        float moveSpeed = 1.f;      // movement speed in units/second
        float rotateSpeed = .005f;  // mouse sensitivity in radians/pixel
    };

    class FPSCamera : public BaseCamera
    {
    public:
        virtual void KeyboardUpdate(int key, int scancode, int action, int mods) override;
        virtual void MousePosUpdate(double xpos, double ypos) override;
        virtual void MouseButtonUpdate(int button, int action, int mods) override;
        virtual void MouseScrollUpdate(double xoffset, double yoffset) override;
        virtual void Animate(float deltaT) override;

        void LookAt(dm::float3 posCamera, dm::float3 posTarget, dm::float3 up = dm::float3{ 0.f, 1.f, 0.f });

    private:
        dm::float2 mousePos;
        dm::float2 mousePosPrev;

        typedef enum
        {
            MoveUp,
            MoveDown,
            MoveLeft,
            MoveRight,
            MoveForward,
            MoveBackward,

            YawRight,
            YawLeft,
            PitchUp,
            PitchDown,
            RollLeft,
            RollRight,

            SpeedUp,
            SlowDown,

            KeyboardControlCount,
        } KeyboardControls;

        typedef enum
        {
            Left,
            Middle,
            Right,

            MouseButtonCount,
            MouseButtonFirst = Left,
        } MouseButtons;

        const std::map<int, int> keyboardMap = {
            { GLFW_KEY_Q, KeyboardControls::MoveDown },
            { GLFW_KEY_E, KeyboardControls::MoveUp },
            { GLFW_KEY_A, KeyboardControls::MoveLeft },
            { GLFW_KEY_D, KeyboardControls::MoveRight },
            { GLFW_KEY_W, KeyboardControls::MoveForward },
            { GLFW_KEY_S, KeyboardControls::MoveBackward },
            { GLFW_KEY_LEFT, KeyboardControls::YawLeft },
            { GLFW_KEY_RIGHT, KeyboardControls::YawRight },
            { GLFW_KEY_UP, KeyboardControls::PitchUp },
            { GLFW_KEY_DOWN, KeyboardControls::PitchDown },
            { GLFW_KEY_Z, KeyboardControls::RollLeft },
            { GLFW_KEY_C, KeyboardControls::RollRight },
            { GLFW_KEY_LEFT_SHIFT, KeyboardControls::SpeedUp },
            { GLFW_KEY_RIGHT_SHIFT, KeyboardControls::SpeedUp },
            { GLFW_KEY_LEFT_CONTROL, KeyboardControls::SlowDown },
            { GLFW_KEY_RIGHT_CONTROL, KeyboardControls::SlowDown },
        };

        const std::map<int, int> mouseButtonMap = {
            { GLFW_MOUSE_BUTTON_LEFT, MouseButtons::Left },
            { GLFW_MOUSE_BUTTON_MIDDLE, MouseButtons::Middle },
            { GLFW_MOUSE_BUTTON_RIGHT, MouseButtons::Right },
        };

        std::array<bool, KeyboardControls::KeyboardControlCount> keyboardState = { false };
        std::array<bool, MouseButtons::MouseButtonCount> mouseButtonState = { false };
    };

}