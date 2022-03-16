#include <donut/app/Camera.h>

using namespace donut::math;
using namespace donut::app;

BaseCamera::BaseCamera()
	: cameraPos(float3(0.f))
	, cameraDir(float3(1.f, 0.f, 0.f))
	, cameraRight(float3(0.f, 0.f, 1.f))
	, cameraUp(0.f, 1.f, 0.f)
	, matWorldToView(affine3::identity())
    , matTranslatedWorldToView(affine3::identity())
{
}

void FPSCamera::KeyboardUpdate(int key, int scancode, int action, int mods)
{
    if (keyboardMap.find(key) == keyboardMap.end())
    {
        return;
    }

    auto cameraKey = keyboardMap.at(key);
    if (action == GLFW_PRESS || action == GLFW_REPEAT)
    {
        keyboardState[cameraKey] = true;
    }
    else {
        keyboardState[cameraKey] = false;
    }
}

void FPSCamera::MousePosUpdate(double xpos, double ypos)
{
    mousePos = { float(xpos), float(ypos) };
}

void FPSCamera::MouseButtonUpdate(int button, int action, int mods)
{
    if (mouseButtonMap.find(button) == mouseButtonMap.end())
    {
        return;
    }

    auto cameraButton = mouseButtonMap.at(button);
    if (action == GLFW_PRESS)
    {
        mouseButtonState[cameraButton] = true;
    }
    else {
        mouseButtonState[cameraButton] = false;
    }
}

void FPSCamera::MouseScrollUpdate(double xoffset, double yoffset)
{
}

void BaseCamera::UpdateWorldToView()
{
    matTranslatedWorldToView = affine3::from_cols(cameraRight, cameraUp, cameraDir, 0.f);
    matWorldToView = translation(-cameraPos) * matTranslatedWorldToView;
}

void BaseCamera::SetMoveSpeed(float moveSpeed)
{
    this->moveSpeed = moveSpeed;
}

void BaseCamera::SetRotateSpeed(float rotateSpeed)
{
    this->rotateSpeed = rotateSpeed;
}

const dm::affine3& BaseCamera::GetWorldToViewMatrix()
{
    return matWorldToView;
}

const dm::affine3& BaseCamera::GetTranslatedWorldToViewMatrix()
{
    return matTranslatedWorldToView;
}

const dm::float3& BaseCamera::GetPosition()
{
	return cameraPos;
}

const dm::float3& BaseCamera::GetDir()
{
	return cameraDir;
}

const dm::float3& BaseCamera::GetUp()
{
	return cameraUp;
}

void BaseCamera::BaseLookAt(float3 cameraPos, float3 cameraTarget, float3 cameraUp)
{
    this->cameraPos = cameraPos;
    this->cameraDir = normalize(cameraTarget - cameraPos);
    this->cameraUp = normalize(cameraUp);
    this->cameraRight = normalize(cross(this->cameraDir, this->cameraUp));
    this->cameraUp = normalize(cross(this->cameraRight, this->cameraDir));

    UpdateWorldToView();
}

void FPSCamera::LookAt(float3 cameraPos, float3 cameraTarget, float3 cameraUp)
{
	// Make the base method public.
	BaseLookAt(cameraPos, cameraTarget, cameraUp);
}

void FPSCamera::Animate(float deltaT)
{
    // track mouse delta
    float2 mouseMove = mousePos - mousePosPrev;
    mousePosPrev = mousePos;

    bool cameraDirty = false;
    affine3 cameraRotation = affine3::identity();

    // handle mouse rotation first
    // this will affect the movement vectors in the world matrix, which we use below
    if (mouseButtonState[MouseButtons::Left] && (mouseMove.x || mouseMove.y))
    {
        float yaw = rotateSpeed * mouseMove.x;
        float pitch = rotateSpeed * mouseMove.y;

        cameraRotation = rotation(float3(0.f, 1.f, 0.f), -yaw);
        cameraRotation = rotation(cameraRight, -pitch) * cameraRotation;

        cameraDirty = true;
    }

    // handle keyboard roll next
    if (keyboardState[KeyboardControls::RollLeft] ||
        keyboardState[KeyboardControls::RollRight])
    {
        float roll = keyboardState[KeyboardControls::RollLeft] * -rotateSpeed * 2.0f +
            keyboardState[KeyboardControls::RollRight] * rotateSpeed * 2.0f;

        cameraRotation = rotation(cameraDir, roll) * cameraRotation;
        cameraDirty = true;
    }

    // handle translation
    float moveStep = deltaT * moveSpeed;
    float3 cameraMoveVec = 0.f;

    if (keyboardState[KeyboardControls::SpeedUp])
        moveStep *= 3.f;

    if (keyboardState[KeyboardControls::SlowDown])
        moveStep *= .1f;

    if (keyboardState[KeyboardControls::MoveForward])
    {
        cameraDirty = true;
        cameraMoveVec += cameraDir * moveStep;
    }

    if (keyboardState[KeyboardControls::MoveBackward])
    {
        cameraDirty = true;
        cameraMoveVec += -cameraDir * moveStep;
    }

    if (keyboardState[KeyboardControls::MoveLeft])
    {
        cameraDirty = true;
        cameraMoveVec += -cameraRight * moveStep;
    }

    if (keyboardState[KeyboardControls::MoveRight])
    {
        cameraDirty = true;
        cameraMoveVec += cameraRight * moveStep;
    }

    if (keyboardState[KeyboardControls::MoveUp])
    {
        cameraDirty = true;
        cameraMoveVec += cameraUp * moveStep;
    }

    if (keyboardState[KeyboardControls::MoveDown])
    {
        cameraDirty = true;
        cameraMoveVec += -cameraUp * moveStep;
    }

    if (cameraDirty)
    {
        cameraPos += cameraMoveVec;
        cameraDir = normalize(cameraRotation.transformVector(cameraDir));
        cameraUp = normalize(cameraRotation.transformVector(cameraUp));
        cameraRight = normalize(cross(cameraDir, cameraUp));

        UpdateWorldToView();
    }
}

