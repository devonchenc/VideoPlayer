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