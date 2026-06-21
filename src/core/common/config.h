#pragma once

// ============================================================================
// Platform Detection
// ============================================================================
#if defined(_WIN32)
#  define V2_PLATFORM_WINDOWS 1
#elif defined(__linux__)
#  define V2_PLATFORM_LINUX 1
#elif defined(__APPLE__)
#  define V2_PLATFORM_MACOS 1
#else
#  error "V2 Engine: Unsupported platform"
#endif

// ============================================================================
// Compiler Detection
// ============================================================================
#if defined(_MSC_VER)
#  define V2_COMPILER_MSVC 1
#elif defined(__clang__)
#  define V2_COMPILER_CLANG 1
#elif defined(__GNUC__)
#  define V2_COMPILER_GCC 1
#endif

// ============================================================================
// Core Engine
// ============================================================================
#ifndef V2_APP_VERSION
#  define V2_APP_VERSION "0.0.1"
#endif
#ifndef V2_DEFAULT_MAILBOX_SIZE
#  define V2_DEFAULT_MAILBOX_SIZE 128
#endif
#ifndef V2_WORKER_MAX_BATCH
#  define V2_WORKER_MAX_BATCH -1
#endif
#ifndef V2_DEFAULT_WORKER_COUNT
#  define V2_DEFAULT_WORKER_COUNT 1
#endif
#ifndef V2_EPOLL_MAX_EVENTS
#  define V2_EPOLL_MAX_EVENTS 64
#endif
#ifndef V2_EPOLL_WAIT_TIMEOUT_MS
#  define V2_EPOLL_WAIT_TIMEOUT_MS 1000
#endif

// ============================================================================
// Feature: IPC (Unix Domain Socket)
// ============================================================================
#ifndef V2_ENABLE_IPC_SERVER_ACTOR
#  define V2_ENABLE_IPC_SERVER_ACTOR 1
#endif
#ifndef V2_IPC_RECV_BUFFER_SIZE
#  define V2_IPC_RECV_BUFFER_SIZE 4096
#endif
#ifndef V2_UDS_DEFAULT_BACKLOG
#  define V2_UDS_DEFAULT_BACKLOG 5
#endif
#ifndef V2_DEFAULT_IPC_SOCKET_PATH
#  define V2_DEFAULT_IPC_SOCKET_PATH "/tmp/v2_ipc.sock"
#endif

// ============================================================================
// Feature: Tick Actor
// ============================================================================
#ifndef V2_ENABLE_TICK_ACTOR
#  define V2_ENABLE_TICK_ACTOR 1
#endif
#ifndef V2_DEFAULT_TICK_INTERVAL_MS
#  define V2_DEFAULT_TICK_INTERVAL_MS 100
#endif

// ============================================================================
// Utilities
// ============================================================================
#ifndef V2_DATE_STRING_BUF_SIZE
#  define V2_DATE_STRING_BUF_SIZE 32
#endif
#ifndef V2_DEFAULT_LOG_LEVEL
/*
    Verbose = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
*/
#  define V2_DEFAULT_LOG_LEVEL 1
#endif
#ifndef V2_MAINAPP_MAINLOOP_SLEEP_MS
#  define V2_MAINAPP_MAINLOOP_SLEEP_MS 100
#endif
