#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <Windows.h>
#include "VideoPlayer.h"

class CPlayerManager
{
public:
    static CPlayerManager& Instance()
    {
        static CPlayerManager instance;
        return instance;
    }

    HRESULT CreatePlayer(HWND parentWnd, long* playerInstance)
    {
        if (!parentWnd || !IsWindow(parentWnd) || !playerInstance)
            return E_INVALIDARG;

        std::shared_ptr<CVideoPlayer> player = std::make_shared<CVideoPlayer>();
        HRESULT hr = player->Initialize(parentWnd);
        if (FAILED(hr))
            return hr;

        const long instance = m_nextPlayerId.fetch_add(1);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_players.emplace(instance, std::move(player));
        }

        *playerInstance = instance;

        return S_OK;
    }

    HRESULT ReleasePlayer(long playerInstance)
    {
        std::shared_ptr<CVideoPlayer> player;

        {
            std::lock_guard<std::mutex> lock(m_mutex);

            auto it = m_players.find(playerInstance);
            if (it == m_players.end())
                return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

            player = std::move(it->second);
            m_players.erase(it);
        }

        player->Shutdown();

        return S_OK;
    }

    std::shared_ptr<CVideoPlayer> GetPlayer(long playerInstance)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        auto it = m_players.find(playerInstance);

        if (it == m_players.end())
            return nullptr;

        return it->second;
    }

    HRESULT SetPlayerWindowPos(long playerInstance, RECT newWindowPos)
    {
        std::shared_ptr<CVideoPlayer> player = GetPlayer(playerInstance);
        if (!player)
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

        return player->SetWindowPos(newWindowPos);
    }

private:
    CPlayerManager() = default;

    std::mutex m_mutex;
    std::unordered_map<long, std::shared_ptr<CVideoPlayer>> m_players;
    std::atomic<long> m_nextPlayerId{ 1 };
};