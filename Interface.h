#pragma once

#include <Windows.h>

HRESULT InitializePlayer();

HRESULT CreatePlayerInstance(HWND playbackWndHandle, HWND callbackWndHandle, long* playerInstance);

HRESULT ReleasePlayerInstance(long playerInstance);

HRESULT SetPlayerWindowPos(long playerInstance, RECT newWindowPos);

HRESULT GetPlayerWindowPos(long playerInstance, RECT* windowPos);

HRESULT SetPlaybackSpeed(long playerInstance, long playSpeed);

HRESULT GetPlaybackSpeed(long playerInstance, long* playSpeed);

HRESULT PlayLive(long playerInstance, long cameraNumber, long playSpeed);

HRESULT PlaySegment(long playerInstance, long cameraNumber, SYSTEMTIME startTime, SYSTEMTIME endTime, long playSpeed);

HRESULT PlayStop(long playerInstance);

HRESULT PlayPause(long playerInstance);

HRESULT PlayResume(long playerInstance);

HRESULT PlayFastForward(long playerInstance);

HRESULT PlayRewind(long playerInstance);

HRESULT PlaySeek(long playerInstance, long secFromStart);

HRESULT GetActualPlayTime(long playerInstance, SYSTEMTIME* startTime, SYSTEMTIME* endTime);

HRESULT GetVideoDimensions(long playerInstance, long* width, long* height);

HRESULT GetCurrentPosition(long playerInstance, hyper* filetimeRelativelyStartTime);

HRESULT StepFrame(long playerInstance, long frames);

HRESULT CanStepFrame(long playerInstance, long flags);
