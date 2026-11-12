#include "Interface.h"

#include "PlayerGlobalContext.h"
#include "PlayerManager.h"

HRESULT InitializePlayer()
{
    return CPlayerGlobalContext::GetInstance().Initialize();;
}

HRESULT CreatePlayerInstance(HWND playbackWndHandle, HWND callbackWndHandle, long* playerInstance)
{
    if (!playbackWndHandle || !playerInstance)
        return E_INVALIDARG;

    CPlayerGlobalContext& context = CPlayerGlobalContext::GetInstance();
    if (!context.IsInitialized())
    {
        HRESULT hr = context.Initialize();
        if (FAILED(hr))
            return hr;
    }

    return CPlayerManager::Instance().CreatePlayer(playbackWndHandle, playerInstance);
}

HRESULT ReleasePlayerInstance(long playerInstance)
{
    return CPlayerManager::Instance().ReleasePlayer(playerInstance);
}

HRESULT SetPlayerWindowPos(long playerInstance, RECT newWindowPos)
{
    return CPlayerManager::Instance().SetPlayerWindowPos(playerInstance, newWindowPos);
}

HRESULT GetPlayerWindowPos(long playerInstance, RECT* windowPos)
{
    return CPlayerManager::Instance().GetPlayerWindowPos(playerInstance, windowPos);
}

HRESULT PlayLive(long playerInstance, long cameraNumber, long playSpeed)
{
    return CPlayerManager::Instance().PlayLive(playerInstance, cameraNumber, playSpeed);
}

HRESULT PlayStop(long playerInstance)
{
    return CPlayerManager::Instance().PlayStop(playerInstance);
}

HRESULT PlayPause(long playerInstance)
{
    return CPlayerManager::Instance().PlayPause(playerInstance);
}

HRESULT PlayResume(long playerInstance)
{
    return CPlayerManager::Instance().PlayResume(playerInstance);
}
