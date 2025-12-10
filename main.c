// main.c  (updated)
//
// Changes made:
// - Win32: create a borderless popup window (WS_POPUP) positioned at the cursor + 640px Y offset
//   and clamped to the working area (so it doesn't overlap taskbar/panels). Added WS_CLIPCHILDREN
//   to avoid children drawing glitches. Window quits on losing focus (WM_ACTIVATE -> WA_INACTIVE).
// - Win32: use SPI_GETWORKAREA to avoid overlapping taskbar. Use GetCursorPos for initial placement.
// - Win32: ensure scrollbar is a child and update clipping behavior. Ensure buttons are shown after reposition.
// - X11: create an override-redirect (borderless) window, position at cursor + 640px Y offset,
//   clamp to _NET_WORKAREA if available (fallback to screen size). Added FocusChangeMask to exit on focus lost.
// - X11: restored vertical list behavior and ensured only visible items are drawn; improved redraw stability.
// - Kept all original function prototypes/variables (no removals).
//
// Note: This is a minimal, conservative patch to restore the requested behaviours while
// keeping the rest of the logic intact.

#ifdef _WIN32
    // Windows platform
    #define UNICODE
    #define _UNICODE
    #include <windows.h>
    #include <shellapi.h>
    #include <wchar.h>
    #include <direct.h> // For _chdir
    #include <unistd.h> // For access()
#else
    // Linux platform
    #include <X11/Xlib.h>
    #include <X11/Xutil.h>
    #include <X11/Xatom.h>
    #include <X11/keysym.h>
    #include <X11/extensions/XShm.h>
    #include <X11/extensions/Xrender.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <dirent.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <ctype.h>
    #include <time.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#define MAX_FILES 2048
#define MAX_PATH_LEN 32767
#define BUTTON_HEIGHT 40
#define BUTTON_WIDTH 260
#define BUTTON_START_Y 110
#define BUTTON_START_ID 2000

// Global argc/argv shared by BOTH Win32 and X11 builds
int g_argc = 0;
char **g_argv = NULL;

#ifdef _WIN32
    #define PATH_SEP '\\'
#else
    #define PATH_SEP '/'
#endif

// Cross-platform case-insensitive string comparison
int stricmp_cross(const char *s1, const char *s2) {
#ifdef _WIN32
    return _stricmp(s1, s2);
#else
    return strcasecmp(s1, s2);
#endif
}

// Platform-independent functions
int IS_CLI() {
    if (access("CLI_MODE", F_OK) == 0) {
        return 0;
    }
    return 1;
}

// Check if path is a directory
int is_directory(const char *path) {
#ifdef _WIN32
    wchar_t wpath[MAX_PATH_LEN];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH_LEN);
    DWORD attr = GetFileAttributesW(wpath);
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    return 0;
#endif
}

// Check if filename matches filters
int matches_filters(const char *filename, int argc, char *argv[], int startIndex) {
    if (argc - startIndex <= 0) return 1; // no filters
    for (int i = startIndex; i < argc; i++)
        if (strstr(filename, argv[i]) != NULL)
            return 1;
    return 0;
}

// Check if string is numeric
int is_number(const char *s) {
    if (!s || *s == '\0') return 0;
    for (; *s; s++)
        if (!isdigit(*s)) return 0;
    return 1;
}

// Clear console
void clear_console() {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

// Remove trailing slashes from a path
void remove_trailing_slash(char *path) {
    size_t len = strlen(path);
    
    // Remove all trailing slashes
    while (len > 0 && (path[len - 1] == '\\' || path[len - 1] == '/')) {
        path[len - 1] = '\0';
        len--;
    }
}

// Set current working directory
int set_cur_dir(const char *path) {
#ifdef _WIN32
    if (_chdir(path) == 0) {
        return 1;
    }
#else
    if (chdir(path) == 0) {
        return 1;
    }
#endif
    return 0;
}

// Scan directory and fill file list
int scan_directory(const char *dirpath, char *files[], int argc, char *argv[], int filterStart) {
#ifdef _WIN32
    WIN32_FIND_DATAW fd;
    wchar_t searchPath[MAX_PATH_LEN];
    wchar_t wdirpath[MAX_PATH_LEN];
    
    MultiByteToWideChar(CP_UTF8, 0, dirpath, -1, wdirpath, MAX_PATH_LEN);
    _snwprintf(searchPath, MAX_PATH_LEN, L"%s\\*", wdirpath);

    HANDLE hFind = FindFirstFileW(searchPath, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return 0;

    int count = 0;
    do {
        const wchar_t *wname = fd.cFileName;
        if (wcscmp(wname, L".") == 0 || wcscmp(wname, L"..") == 0) continue;

        char name[MAX_PATH_LEN];
        WideCharToMultiByte(CP_UTF8, 0, wname, -1, name, MAX_PATH_LEN, NULL, NULL);

        if (matches_filters(name, argc, argv, filterStart) && count < MAX_FILES)
            files[count++] = strdup(name);

    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
    return count;
#else
    DIR *dir = opendir(dirpath);
    if (!dir) return 0;

    struct dirent *entry;
    int count = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        if (matches_filters(entry->d_name, argc, argv, filterStart) && count < MAX_FILES)
            files[count++] = strdup(entry->d_name);
    }

    closedir(dir);
    return count;
#endif
}

// Print documentation
void print_documentation() {
    clear_console();
    printf("Better Toolbar CLI\n");
    printf("Navigate folders, open files, supports absolute paths.\n");
    printf("Usage:\n");
    printf("  better-toolbar.exe [folder] [filters...]\n\n");
    printf("Examples:\n");
    printf("  better-toolbar.exe /home/user/Documents\n");
    printf("  better-toolbar.exe . .txt .pdf\n");
    printf("  better-toolbar.exe /home/user/Projects .cpp .h\n");
}

// CLI mode function
int main_cli_function(int argc, char *argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    char dirpath[MAX_PATH_LEN];
    int filterStart = 1;

    // Determine scanning directory
    if (argc >= 2) {
        char candidatePath[MAX_PATH_LEN];
        
        // Copy the argument
        strncpy(candidatePath, argv[1], MAX_PATH_LEN - 1);
        candidatePath[MAX_PATH_LEN - 1] = '\0';
        remove_trailing_slash(candidatePath);

        // Check if it's a directory
        if (is_directory(candidatePath)) {
            // Valid directory - try to change to it
            if (set_cur_dir(candidatePath)) {
                // Success - get the absolute path and use it
#ifdef _WIN32
                wchar_t wdirpath[MAX_PATH_LEN];
                GetCurrentDirectoryW(MAX_PATH_LEN, wdirpath);
                WideCharToMultiByte(CP_UTF8, 0, wdirpath, -1, dirpath, MAX_PATH_LEN, NULL, NULL);
#else
                getcwd(dirpath, MAX_PATH_LEN);
#endif
                remove_trailing_slash(dirpath);
                filterStart = 2; // Filters start at argv[2]
            } else {
                // Failed to change directory
                printf("Error: Cannot access directory '%s'\n", candidatePath);
                // Use current directory as fallback
#ifdef _WIN32
                wchar_t wdirpath[MAX_PATH_LEN];
                GetCurrentDirectoryW(MAX_PATH_LEN, wdirpath);
                WideCharToMultiByte(CP_UTF8, 0, wdirpath, -1, dirpath, MAX_PATH_LEN, NULL, NULL);
#else
                getcwd(dirpath, MAX_PATH_LEN);
#endif
                remove_trailing_slash(dirpath);
                filterStart = 1; // Treat argv[1] as filter
            }
        } else {
            // Not a directory - use current dir, treat argv[1] as filter
#ifdef _WIN32
            wchar_t wdirpath[MAX_PATH_LEN];
            GetCurrentDirectoryW(MAX_PATH_LEN, wdirpath);
            WideCharToMultiByte(CP_UTF8, 0, wdirpath, -1, dirpath, MAX_PATH_LEN, NULL, NULL);
#else
            getcwd(dirpath, MAX_PATH_LEN);
#endif
            remove_trailing_slash(dirpath);
            filterStart = 1;
        }
    } else {
        // No arguments - use current directory
#ifdef _WIN32
        wchar_t wdirpath[MAX_PATH_LEN];
        GetCurrentDirectoryW(MAX_PATH_LEN, wdirpath);
        WideCharToMultiByte(CP_UTF8, 0, wdirpath, -1, dirpath, MAX_PATH_LEN, NULL, NULL);
#else
        getcwd(dirpath, MAX_PATH_LEN);
#endif
        remove_trailing_slash(dirpath);
    }

    char *files[MAX_FILES];
    int fileCount = 0;

    while (1) {
        // Cleanup previous scan results
        for (int i = 0; i < fileCount; i++) free(files[i]);
        fileCount = scan_directory(dirpath, files, argc, argv, filterStart);

        clear_console();
        printf("Current directory: %s\n", dirpath);

        if (fileCount == 0)
            printf("No matching files found.\n");
        else {
            printf("Found files:\n");
            for (int i = 0; i < fileCount; i++)
                printf("[%d] %s\n", i, files[i]);
        }

        printf("\nEnter index, 'up' to go up, d/D for docs, q/Q to quit: ");
        char input[64];
        if (!fgets(input, sizeof(input), stdin)) continue;
        input[strlen(input)-1] = '\0'; // strip newline

        if (strlen(input) == 1) {
            if (input[0] == 'q' || input[0] == 'Q') break;
            if (input[0] == 'd' || input[0] == 'D') {
                print_documentation();
                printf("\nPress Enter to continue...");
                getchar(); 
                continue;
            }
        }

        if (stricmp_cross(input, "up") == 0) {
            char tempPath[MAX_PATH_LEN];
            strcpy(tempPath, dirpath);
            
            char *lastSlash = strrchr(tempPath, '\\');
            if (!lastSlash) lastSlash = strrchr(tempPath, '/');
            
            if (lastSlash) {
                *lastSlash = '\0';
                
                // Try to change to parent directory
                if (set_cur_dir(tempPath)) {
#ifdef _WIN32
                    wchar_t wdirpath[MAX_PATH_LEN];
                    GetCurrentDirectoryW(MAX_PATH_LEN, wdirpath);
                    WideCharToMultiByte(CP_UTF8, 0, wdirpath, -1, dirpath, MAX_PATH_LEN, NULL, NULL);
#else
                    getcwd(dirpath, MAX_PATH_LEN);
#endif
                    remove_trailing_slash(dirpath);
                }
            }
            // If no slash found, we're already at root - stay here
            continue;
        }

        if (!is_number(input)) {
            printf("Invalid input!\n");
            sleep(1000);
            continue;
        }

        int index = atoi(input);
        if (index < 0 || index >= fileCount) {
            printf("Index out of range!\n");
            sleep(1000);
            continue;
        }

        char fullPath[MAX_PATH_LEN];
        snprintf(fullPath, MAX_PATH_LEN, "%s%c%s", dirpath, PATH_SEP, files[index]);

        if (is_directory(fullPath)) {
            // Change scanning directory to subdirectory
            if (set_cur_dir(fullPath)) {
#ifdef _WIN32
                wchar_t wdirpath[MAX_PATH_LEN];
                GetCurrentDirectoryW(MAX_PATH_LEN, wdirpath);
                WideCharToMultiByte(CP_UTF8, 0, wdirpath, -1, dirpath, MAX_PATH_LEN, NULL, NULL);
#else
                getcwd(dirpath, MAX_PATH_LEN);
#endif
                remove_trailing_slash(dirpath);
            }
            continue;
        }

        // Open file with default application
#ifdef _WIN32
        wchar_t wfullPath[MAX_PATH_LEN];
        MultiByteToWideChar(CP_UTF8, 0, fullPath, -1, wfullPath, MAX_PATH_LEN);
        ShellExecuteW(NULL, L"open", wfullPath, NULL, NULL, SW_SHOWNORMAL);
#else
        char cmd[MAX_PATH_LEN * 2];
        snprintf(cmd, sizeof(cmd), "xdg-open '%s' &", fullPath);
        system(cmd);
#endif
    }

    // Final cleanup
    for (int i = 0; i < fileCount; i++) free(files[i]);
    printf("Exiting.\n");
    return 0;
}

// ============ WINDOWS IMPLEMENTATION ============
#ifdef _WIN32

HWND add_button(const wchar_t *label, HWND parent, int x, int y, int width, int height, int id)
{
    HWND h = CreateWindowW(
        L"BUTTON",                  // Predefined class: Button
        label,                      // Button text
        WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,  // Styles
        x, y,                       // Position
        width, height,              // Size
        parent,                     // Parent window
        (HMENU)(intptr_t)id,        // Control ID (cast to HMENU)
        GetModuleHandle(NULL),      // Instance handle
        NULL                        // No parameter
    );
    // Ensure visible and updated
    if (h) {
        ShowWindow(h, SW_SHOW);
        UpdateWindow(h);
    }
    return h;
}

// Global variables for Windows GUI
typedef struct {
    char dirpath[MAX_PATH_LEN];
    char *files[MAX_FILES];
    int fileCount;
    int filterStart;
    HWND fileButtons[MAX_FILES];
    HWND hwndMain;
    HWND hwndScrollbar;
    int scrollPos;
    int windowHeight;
    int windowWidth;
} WindowsAppState;

WindowsAppState g_win_state = {0};

// Destroy all file buttons
void destroy_file_buttons() {
    for (int i = 0; i < g_win_state.fileCount; i++) {
        if (g_win_state.fileButtons[i]) {
            DestroyWindow(g_win_state.fileButtons[i]);
            g_win_state.fileButtons[i] = NULL;
        }
    }
}

// Update scrollbar range based on content
void update_scrollbar() {
    if (!g_win_state.hwndScrollbar) return;
    
    RECT clientRect;
    GetClientRect(g_win_state.hwndMain, &clientRect);
    int clientHeight = clientRect.bottom - BUTTON_START_Y;
    
    int totalContentHeight = g_win_state.fileCount * BUTTON_HEIGHT;
    int maxScroll = totalContentHeight - clientHeight;
    
    if (maxScroll < 0) maxScroll = 0;
    
    SCROLLINFO si = {0};
    si.cbSize = sizeof(SCROLLINFO);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = totalContentHeight;
    si.nPage = clientHeight;
    si.nPos = g_win_state.scrollPos;
    
    SetScrollInfo(g_win_state.hwndScrollbar, SB_CTL, &si, TRUE);
}

// Create file buttons dynamically
void create_file_buttons(int argc, char *argv[]) {
    // Destroy old buttons first
    destroy_file_buttons();
    
    // Free old file list
    for (int i = 0; i < g_win_state.fileCount; i++) {
        if (g_win_state.files[i]) {
            free(g_win_state.files[i]);
            g_win_state.files[i] = NULL;
        }
    }
    
    // Scan directory
    g_win_state.fileCount = scan_directory(g_win_state.dirpath, g_win_state.files, argc, argv, g_win_state.filterStart);
    
    // Create new buttons for each file
    for (int i = 0; i < g_win_state.fileCount; i++) {
        wchar_t wlabel[MAX_PATH_LEN];
        MultiByteToWideChar(CP_UTF8, 0, g_win_state.files[i], -1, wlabel, MAX_PATH_LEN);
        
        int yPos = BUTTON_START_Y + (i * BUTTON_HEIGHT) - g_win_state.scrollPos;
        
        g_win_state.fileButtons[i] = CreateWindowW(
            L"BUTTON",
            wlabel,
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | BS_PUSHBUTTON,
            10, yPos,
            BUTTON_WIDTH, BUTTON_HEIGHT - 5,
            g_win_state.hwndMain,
            (HMENU)(intptr_t)(BUTTON_START_ID + i),
            GetModuleHandle(NULL),
            NULL
        );

        // Force show to avoid transient disappearing due to z-order/clip issues
        if (g_win_state.fileButtons[i]) {
            ShowWindow(g_win_state.fileButtons[i], SW_SHOW);
            UpdateWindow(g_win_state.fileButtons[i]);
        }
    }
    
    // Update scrollbar
    update_scrollbar();
    
    // Redraw window
    InvalidateRect(g_win_state.hwndMain, NULL, TRUE);
    UpdateWindow(g_win_state.hwndMain);
}

// Reposition file buttons based on scroll position
void reposition_file_buttons() {
    InvalidateRect(g_win_state.hwndMain, NULL, TRUE);
    for (int i = 0; i < g_win_state.fileCount; i++) {
        if (g_win_state.fileButtons[i]) {
            int yPos = BUTTON_START_Y + (i * BUTTON_HEIGHT) - g_win_state.scrollPos;
            SetWindowPos(g_win_state.fileButtons[i], NULL, 10, yPos, 0, 0, 
                        SWP_NOSIZE | SWP_NOZORDER);
            ShowWindow(g_win_state.fileButtons[i], SW_SHOW);
        }
    }
}

// Forward declarations for X11 interop functions used by Win32 handlers (keep prototypes)
#ifndef _WIN32
void handle_file_button_click(int buttonIndex);
void handle_up_button();
#endif

// Handle file button click (Win32): reuse the same functionality as X11 handlers where appropriate.
// We'll implement a thin wrapper that uses g_win_state.dirpath etc.
void handle_file_button_click_win32(int buttonIndex) {
    if (buttonIndex < 0 || buttonIndex >= g_win_state.fileCount) return;
    
    char fullPath[MAX_PATH_LEN];
    snprintf(fullPath, MAX_PATH_LEN, "%s%c%s", g_win_state.dirpath, PATH_SEP, g_win_state.files[buttonIndex]);
    
    if (is_directory(fullPath)) {
        // Navigate into directory
        if (set_cur_dir(fullPath)) {
            #ifdef _WIN32
            wchar_t wdirpath[MAX_PATH_LEN];
            GetCurrentDirectoryW(MAX_PATH_LEN, wdirpath);
            WideCharToMultiByte(CP_UTF8, 0, wdirpath, -1, g_win_state.dirpath, MAX_PATH_LEN, NULL, NULL);
            #else
            getcwd(g_win_state.dirpath, MAX_PATH_LEN);
            #endif
            remove_trailing_slash(g_win_state.dirpath);
            
            // Reset scroll position
            g_win_state.scrollPos = 0;
            
            // Refresh the file list
            for (int i = 0; i < g_win_state.fileCount; i++) {
                if (g_win_state.files[i]) {
                    free(g_win_state.files[i]);
                    g_win_state.files[i] = NULL;
                }
            }
            g_win_state.fileCount = scan_directory(g_win_state.dirpath, g_win_state.files, g_argc, g_argv, g_win_state.filterStart);
            
            create_file_buttons(g_argc, g_argv);
        }
    } else {
        // Open file with default application
        wchar_t wfullPath[MAX_PATH_LEN];
        MultiByteToWideChar(CP_UTF8, 0, fullPath, -1, wfullPath, MAX_PATH_LEN);
        ShellExecuteW(NULL, L"open", wfullPath, NULL, NULL, SW_SHOWNORMAL);
    }
}

// Handle "Up" button - go to parent directory (Win32)
void handle_up_button_win32() {
    char tempPath[MAX_PATH_LEN];
    strcpy(tempPath, g_win_state.dirpath);
    
    char *lastSlash = strrchr(tempPath, '\\');
    if (!lastSlash) lastSlash = strrchr(tempPath, '/');
    
    if (lastSlash) {
        *lastSlash = '\0';
        
        // Try to change to parent directory
        if (set_cur_dir(tempPath)) {
            wchar_t wdirpath[MAX_PATH_LEN];
            GetCurrentDirectoryW(MAX_PATH_LEN, wdirpath);
            WideCharToMultiByte(CP_UTF8, 0, wdirpath, -1, g_win_state.dirpath, MAX_PATH_LEN, NULL, NULL);
            remove_trailing_slash(g_win_state.dirpath);
            
            // Reset scroll position
            g_win_state.scrollPos = 0;
            
            // Refresh the file list
            for (int i = 0; i < g_win_state.fileCount; i++) {
                if (g_win_state.files[i]) {
                    free(g_win_state.files[i]);
                    g_win_state.files[i] = NULL;
                }
            }
            g_win_state.fileCount = scan_directory(g_win_state.dirpath, g_win_state.files, g_argc, g_argv, g_win_state.filterStart);
            
            create_file_buttons(g_argc, g_argv);
        }
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

int main_gui_function(int argc, char *argv[], HINSTANCE hInstance, int nCmdShow, wchar_t** argvW) {
    // Store argc/argv globally for later use
    g_argc = argc;
    g_argv = argv;
    
    const wchar_t CLASS_NAME[] = L"BasicWindowClass";

    WNDCLASS wc = {0};
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    // Use a null brush so we can control background and avoid child flicker; clip children will help
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;

    RegisterClass(&wc);

    wchar_t title[MAX_PATH_LEN];
    MultiByteToWideChar(CP_UTF8, 0, "Better-Toolbar", -1, title, MAX_PATH_LEN);

    // Initialize directory path
    g_win_state.filterStart = 1;
    
    if (argc >= 2) {
        char candidatePath[MAX_PATH_LEN];
        
        // Copy the argument
        strncpy(candidatePath, argv[1], MAX_PATH_LEN - 1);
        candidatePath[MAX_PATH_LEN - 1] = '\0';
        remove_trailing_slash(candidatePath);

        // Check if it's a directory
        if (is_directory(candidatePath)) {
            if (set_cur_dir(candidatePath)) {
                wchar_t wdirpath[MAX_PATH_LEN];
                GetCurrentDirectoryW(MAX_PATH_LEN, wdirpath);
                WideCharToMultiByte(CP_UTF8, 0, wdirpath, -1, g_win_state.dirpath, MAX_PATH_LEN, NULL, NULL);
                remove_trailing_slash(g_win_state.dirpath);
                g_win_state.filterStart = 2;
            }
        }
    }
    
    // If not set, use current directory
    if (strlen(g_win_state.dirpath) == 0) {
        wchar_t wdirpath[MAX_PATH_LEN];
        GetCurrentDirectoryW(MAX_PATH_LEN, wdirpath);
        WideCharToMultiByte(CP_UTF8, 0, wdirpath, -1, g_win_state.dirpath, MAX_PATH_LEN, NULL, NULL);
        remove_trailing_slash(g_win_state.dirpath);
    }

    // Determine initial position: at cursor with +640 Y offset, clamped to work area (avoids taskbar)
    POINT cursorPos;
    GetCursorPos(&cursorPos);
    RECT workArea;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);

    int winW = 300;
    int winH = 600;
    int createX = cursorPos.x;
    int createY = cursorPos.y - 640;

    if (createX + winW > workArea.right) createX = workArea.right - winW;
    if (createY + winH > workArea.bottom) createY = workArea.bottom - winH;
    if (createX < workArea.left) createX = workArea.left;
    if (createY < workArea.top) createY = workArea.top;

    // Create a borderless popup window (tool window so not shown in taskbar) with clipchildren to reduce drawing artifacts
    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        CLASS_NAME,
        title,
        WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN,
        createX, createY, winW, winH,
        NULL, NULL, hInstance, NULL
    );

    if (hwnd == NULL) {
        return 1;
    }

    g_win_state.hwndMain = hwnd;
    g_win_state.scrollPos = 0;

    // Create scrollbar as child control and ensure it's visible
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    g_win_state.hwndScrollbar = CreateWindowW(
        L"SCROLLBAR",
        NULL,
        WS_CHILD | WS_VISIBLE | SBS_VERT,
        winW - 20, BUTTON_START_Y,
        20, winH - BUTTON_START_Y,
        hwnd,
        (HMENU)9999,
        hInstance,
        NULL
    );

    add_button(L"Up",           hwnd, 10,  10,  80, 40, 1001);
    add_button(L"Refresh",      hwnd, 100, 10,  80, 40, 1002);
    add_button(L"Quit",         hwnd, 190, 10,  80, 40, 1003);

    // Initial scan and button creation
    create_file_buttons(argc, argv);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup
    destroy_file_buttons();
    for (int i = 0; i < g_win_state.fileCount; i++) {
        if (g_win_state.files[i]) free(g_win_state.files[i]);
    }

    return (int)msg.wParam;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
        case WM_COMMAND: {
            int id = LOWORD(wParam);  // Button ID

            switch (id) {
                case 1001:  // Up button
                    handle_up_button_win32();
                    return 0;

                case 1002:  // Refresh
                    g_win_state.scrollPos = 0;
                    create_file_buttons(g_argc, g_argv);
                    return 0;

                case 1003:  // Quit
                    PostQuitMessage(0);
                    return 0;
                
                default:
                    // Check if it's a file button
                    if (id >= BUTTON_START_ID && id < BUTTON_START_ID + MAX_FILES) {
                        int buttonIndex = id - BUTTON_START_ID;
                        handle_file_button_click_win32(buttonIndex);
                    }
                    return 0;
            }
            break;
        }

        case WM_VSCROLL: {
            SCROLLINFO si = {0};
            si.cbSize = sizeof(SCROLLINFO);
            si.fMask = SIF_ALL;
            GetScrollInfo(g_win_state.hwndScrollbar, SB_CTL, &si);
            
            int oldPos = si.nPos;
            
            switch (LOWORD(wParam)) {
                case SB_LINEUP:
                    si.nPos -= BUTTON_HEIGHT;
                    break;
                case SB_LINEDOWN:
                    si.nPos += BUTTON_HEIGHT;
                    break;
                case SB_PAGEUP:
                    si.nPos -= si.nPage;
                    break;
                case SB_PAGEDOWN:
                    si.nPos += si.nPage;
                    break;
                case SB_THUMBTRACK:
                    si.nPos = si.nTrackPos;
                    break;
            }
            
            // Ensure position is within bounds
            si.fMask = SIF_POS;
            SetScrollInfo(g_win_state.hwndScrollbar, SB_CTL, &si, TRUE);
            GetScrollInfo(g_win_state.hwndScrollbar, SB_CTL, &si);
            
            if (si.nPos != oldPos) {
                g_win_state.scrollPos = si.nPos;
                reposition_file_buttons();
            }
            
            return 0;
        }

        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            SCROLLINFO si = {0};
            si.cbSize = sizeof(SCROLLINFO);
            si.fMask = SIF_ALL;
            GetScrollInfo(g_win_state.hwndScrollbar, SB_CTL, &si);
            
            int oldPos = si.nPos;
            si.nPos -= (delta / WHEEL_DELTA) * BUTTON_HEIGHT;
            
            si.fMask = SIF_POS;
            SetScrollInfo(g_win_state.hwndScrollbar, SB_CTL, &si, TRUE);
            GetScrollInfo(g_win_state.hwndScrollbar, SB_CTL, &si);
            
            if (si.nPos != oldPos) {
                g_win_state.scrollPos = si.nPos;
                reposition_file_buttons();
            }
            
            return 0;
        }

        case WM_SIZE: {
            // Resize scrollbar when window is resized
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            
            if (g_win_state.hwndScrollbar) {
                SetWindowPos(g_win_state.hwndScrollbar, NULL,
                            clientRect.right - 20, BUTTON_START_Y,
                            20, clientRect.bottom - BUTTON_START_Y,
                            SWP_NOZORDER);
                update_scrollbar();
            }
            return 0;
        }

        case WM_ACTIVATE: {
            // If we lost activation/focus, quit (original behavior expected by user)
            if (LOWORD(wParam) == WA_INACTIVE) {
                PostQuitMessage(0);
            }
            return 0;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            // Properly clip the file button area
            HRGN clip = CreateRectRgn(0, BUTTON_START_Y,
                                     g_win_state.windowWidth - 20,   // leave space for scrollbar
                                     g_win_state.windowHeight);
            SelectClipRgn(hdc, clip);

            // Draw directory text (inside clipped area)
            wchar_t wdirpath[MAX_PATH_LEN];
            MultiByteToWideChar(CP_UTF8, 0, g_win_state.dirpath, -1, wdirpath, MAX_PATH_LEN);
            TextOutW(hdc, 10, 60, wdirpath, wcslen(wdirpath));

            // Buttons are children - they are automatically clipped by WS_CLIPCHILDREN on the parent
            // but we also ensure no overdrawing here

            SelectClipRgn(hdc, NULL);
            DeleteObject(clip);

            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // Get the full command line as wide string
    wchar_t* cmdLine = GetCommandLineW();

    int argc;
    wchar_t** argvW = CommandLineToArgvW(cmdLine, &argc);
    if (argvW == NULL) {
        MessageBoxW(NULL, L"Failed to parse command line", L"Error", MB_ICONERROR);
        return 1;
    }

    // Convert argvW to UTF-8 char** 
    char** argv = (char**)calloc(argc, sizeof(char*));
    for (int i = 0; i < argc; ++i) {
        int size = WideCharToMultiByte(CP_UTF8, 0, argvW[i], -1, NULL, 0, NULL, NULL);
        argv[i] = (char*)malloc(size);
        WideCharToMultiByte(CP_UTF8, 0, argvW[i], -1, argv[i], size, NULL, NULL);
    }

    int result = 0;

    if (IS_CLI() == 0) {
        // Allocate a console for CLI mode (needed when compiled with -mwindows)
        AllocConsole();
        
        // Redirect standard streams to the new console
        FILE* fp;
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        freopen("CONIN$", "r", stdin);
        
        result = main_cli_function(argc, argv);
        
        // Free the console when done
        FreeConsole();
    } else {
        result = main_gui_function(argc, argv, hInstance, nCmdShow, argvW);
    }

    // Cleanup
    LocalFree(argvW);
    for (int i = 0; i < argc; ++i) free(argv[i]);
    free(argv);

    return result;
}

#endif  // _WIN32

// ============ LINUX X11 IMPLEMENTATION ============
#ifndef _WIN32

// X11 state structure
typedef struct {
    Display *display;
    Window window;
    GC gc;
    char dirpath[MAX_PATH_LEN];
    char *files[MAX_FILES];
    int fileCount;
    int filterStart;
    int scrollPos;
    int windowWidth;
    int windowHeight;
    int mouseX, mouseY;
    int buttonPressed;
    int quitFlag;
} X11AppState;

X11AppState g_x11_state = {0};

// Free files
void free_files() {
    for (int i = 0; i < g_x11_state.fileCount; i++) {
        if (g_x11_state.files[i]) {
            free(g_x11_state.files[i]);
            g_x11_state.files[i] = NULL;
        }
    }
    g_x11_state.fileCount = 0;
}

// Draw text at position
void draw_text(Display *display, Window window, GC gc, int x, int y, const char *text) {
    XDrawString(display, window, gc, x, y + 12, text, strlen(text));
}

// Draw a button
void draw_button(Display *display, Window window, GC gc, int x, int y, int width, int height, const char *label, int isPressed) {
    // Draw button background
    if (isPressed) {
        XSetForeground(display, gc, 0x888888);
    } else {
        XSetForeground(display, gc, 0xDDDDDD);
    }
    XFillRectangle(display, window, gc, x, y, width, height);
    
    // Draw border
    XSetForeground(display, gc, 0x000000);
    XDrawRectangle(display, window, gc, x, y, width - 1, height - 1);
    
    // Draw text
    XSetForeground(display, gc, 0x000000);
    XDrawString(display, window, gc, x + 10, y + 12, label, strlen(label));
}

// Draw the entire window
void draw_window() {
    if (!g_x11_state.display) return;
    
    // Clear window
    XSetForeground(g_x11_state.display, g_x11_state.gc, 0xFFFFFF);
    XFillRectangle(g_x11_state.display, g_x11_state.window, g_x11_state.gc, 0, 0, g_x11_state.windowWidth, g_x11_state.windowHeight);
    
    // Draw current directory path
    XSetForeground(g_x11_state.display, g_x11_state.gc, 0x000000);
    draw_text(g_x11_state.display, g_x11_state.window, g_x11_state.gc, 10, 60, g_x11_state.dirpath);
    
    // Draw control buttons
    draw_button(g_x11_state.display, g_x11_state.window, g_x11_state.gc, 10, 10, 80, 40, "Up", 0);
    draw_button(g_x11_state.display, g_x11_state.window, g_x11_state.gc, 100, 10, 80, 40, "Refresh", 0);
    draw_button(g_x11_state.display, g_x11_state.window, g_x11_state.gc, 190, 10, 80, 40, "Quit", 0);
    
    // === CLIPPING FOR FILE BUTTONS ===
    XRectangle clip_rect;
    clip_rect.x = 0;
    clip_rect.y = BUTTON_START_Y;
    clip_rect.width = g_x11_state.windowWidth - 20;  // leave space for scrollbar
    clip_rect.height = g_x11_state.windowHeight - BUTTON_START_Y;
    XSetClipRectangles(g_x11_state.display, g_x11_state.gc, 0, 0, &clip_rect, 1, Unsorted);

    // Draw file buttons (vertical list) - now properly clipped
    for (int i = 0; i < g_x11_state.fileCount; i++) {
        int yPos = BUTTON_START_Y + (i * BUTTON_HEIGHT) - g_x11_state.scrollPos;
        
        // Only draw if at least partially visible (extra safety)
        if (yPos + BUTTON_HEIGHT > BUTTON_START_Y && yPos < g_x11_state.windowHeight) {
            int isPressed = (g_x11_state.buttonPressed == i + 1);
            draw_button(g_x11_state.display, g_x11_state.window, g_x11_state.gc,
                        10, yPos, BUTTON_WIDTH, BUTTON_HEIGHT - 5, g_x11_state.files[i], isPressed);
        }
    }

    // Remove clipping for scrollbar and other elements
    XSetClipMask(g_x11_state.display, g_x11_state.gc, None);
    
    // Draw scrollbar visual (outside clipped area)
    int clientHeight = g_x11_state.windowHeight - BUTTON_START_Y;
    int totalContentHeight = g_x11_state.fileCount * BUTTON_HEIGHT;
    int maxScroll = totalContentHeight - clientHeight;
    
    if (maxScroll > 0) {
        XSetForeground(g_x11_state.display, g_x11_state.gc, 0xAAAAAA);
        XFillRectangle(g_x11_state.display, g_x11_state.window, g_x11_state.gc,
                       g_x11_state.windowWidth - 20, BUTTON_START_Y, 20, clientHeight);
        
        int thumbHeight = (clientHeight * clientHeight) / totalContentHeight;
        if (thumbHeight < 20) thumbHeight = 20;
        int thumbY = BUTTON_START_Y + (g_x11_state.scrollPos * (clientHeight - thumbHeight)) / maxScroll;
        
        XSetForeground(g_x11_state.display, g_x11_state.gc, 0x666666);
        XFillRectangle(g_x11_state.display, g_x11_state.window, g_x11_state.gc,
                       g_x11_state.windowWidth - 20, thumbY, 20, thumbHeight);
    }
    
    XFlush(g_x11_state.display);
}

// Check if point is inside a button
int is_point_in_button(int x, int y, int buttonX, int buttonY, int buttonWidth, int buttonHeight) {
    return (x >= buttonX && x <= buttonX + buttonWidth && y >= buttonY && y <= buttonY + buttonHeight);
}

// Handle file button click
void handle_file_button_click(int buttonIndex) {
    if (buttonIndex < 0 || buttonIndex >= g_x11_state.fileCount) return;
    
    char fullPath[MAX_PATH_LEN];
    snprintf(fullPath, MAX_PATH_LEN, "%s%c%s", g_x11_state.dirpath, PATH_SEP, g_x11_state.files[buttonIndex]);
    
    if (is_directory(fullPath)) {
        // Navigate into directory
        if (set_cur_dir(fullPath)) {
            getcwd(g_x11_state.dirpath, MAX_PATH_LEN);
            remove_trailing_slash(g_x11_state.dirpath);
            
            // Reset scroll position
            g_x11_state.scrollPos = 0;
            
            // Refresh the file list
            free_files();
            g_x11_state.fileCount = scan_directory(g_x11_state.dirpath, g_x11_state.files, g_argc, g_argv, g_x11_state.filterStart);
            
            draw_window();
        }
    } else {
        // Open file with default application
        char cmd[MAX_PATH_LEN * 2];
        snprintf(cmd, sizeof(cmd), "xdg-open '%s' &", fullPath);
        system(cmd);
    }
}

// Handle "Up" button - go to parent directory
void handle_up_button() {
    char tempPath[MAX_PATH_LEN];
    strcpy(tempPath, g_x11_state.dirpath);
    
    char *lastSlash = strrchr(tempPath, '/');
    
    if (lastSlash) {
        *lastSlash = '\0';
        
        // Try to change to parent directory
        if (set_cur_dir(tempPath)) {
            getcwd(g_x11_state.dirpath, MAX_PATH_LEN);
            remove_trailing_slash(g_x11_state.dirpath);
            
            // Reset scroll position
            g_x11_state.scrollPos = 0;
            
            // Refresh the file list
            free_files();
            g_x11_state.fileCount = scan_directory(g_x11_state.dirpath, g_x11_state.files, g_argc, g_argv, g_x11_state.filterStart);
            
            draw_window();
        }
    }
}

// Handle mouse scroll
void handle_mouse_scroll(int delta) {
    int clientHeight = g_x11_state.windowHeight - BUTTON_START_Y;
    int totalContentHeight = g_x11_state.fileCount * BUTTON_HEIGHT;
    int maxScroll = totalContentHeight - clientHeight;
    
    if (maxScroll <= 0) return;
    
    g_x11_state.scrollPos += delta * BUTTON_HEIGHT;
    if (g_x11_state.scrollPos < 0) g_x11_state.scrollPos = 0;
    if (g_x11_state.scrollPos > maxScroll) g_x11_state.scrollPos = maxScroll;
    
    draw_window();
}

// Handle mouse button press
void handle_mouse_press(int x, int y) {
    // Check control buttons
    if (is_point_in_button(x, y, 10, 10, 80, 40)) {
        handle_up_button();
        return;
    }
    if (is_point_in_button(x, y, 100, 10, 80, 40)) {
        g_x11_state.scrollPos = 0;
        free_files();
        g_x11_state.fileCount = scan_directory(g_x11_state.dirpath, g_x11_state.files, g_argc, g_argv, g_x11_state.filterStart);
        draw_window();
        return;
    }
    if (is_point_in_button(x, y, 190, 10, 80, 40)) {
        g_x11_state.quitFlag = 1;
        return;
    }
    
    // Check file buttons
    for (int i = 0; i < g_x11_state.fileCount; i++) {
        int yPos = BUTTON_START_Y + (i * BUTTON_HEIGHT) - g_x11_state.scrollPos;
        if (yPos + BUTTON_HEIGHT > 0 && yPos < g_x11_state.windowHeight) {
            if (is_point_in_button(x, y, 10, yPos, BUTTON_WIDTH, BUTTON_HEIGHT - 5)) {
                g_x11_state.buttonPressed = i + 1;
                draw_window();
                return;
            }
        }
    }
}

// Handle mouse button release
void handle_mouse_release(int x, int y) {
    // Check if we're still over the same button
    if (g_x11_state.buttonPressed > 0) {
        int buttonIndex = g_x11_state.buttonPressed - 1;
        int yPos = BUTTON_START_Y + (buttonIndex * BUTTON_HEIGHT) - g_x11_state.scrollPos;
        
        if (is_point_in_button(x, y, 10, yPos, BUTTON_WIDTH, BUTTON_HEIGHT - 5)) {
            handle_file_button_click(buttonIndex);
        }
        
        g_x11_state.buttonPressed = 0;
        draw_window();
    }
}

// Handle mouse move
void handle_mouse_move(int x, int y) {
    g_x11_state.mouseX = x;
    g_x11_state.mouseY = y;
}

// Cleanup X11 resources
void cleanup_x11() {
    free_files();
    if (g_x11_state.display) {
        if (g_x11_state.window) {
            XDestroyWindow(g_x11_state.display, g_x11_state.window);
        }
        if (g_x11_state.gc) {
            XFreeGC(g_x11_state.display, g_x11_state.gc);
        }
        XCloseDisplay(g_x11_state.display);
    }
}

int main_gui_function(int argc, char *argv[]) {
    // Store argc/argv globally
    g_argc = argc;
    g_argv = argv;
    
    // Initialize X11
    g_x11_state.display = XOpenDisplay(NULL);
    if (!g_x11_state.display) {
        fprintf(stderr, "Error: Cannot open X11 display\n");
        return 1;
    }
    
    int screen = DefaultScreen(g_x11_state.display);
    Window root = RootWindow(g_x11_state.display, screen);
    
    // default window size
    g_x11_state.windowWidth = 300;
    g_x11_state.windowHeight = 600;

    // Determine work area using _NET_WORKAREA (if available) to avoid overlapping panels/taskbar
    long work_x = 0, work_y = 0, work_w = DisplayWidth(g_x11_state.display, screen), work_h = DisplayHeight(g_x11_state.display, screen);
    Atom netWorkarea = XInternAtom(g_x11_state.display, "_NET_WORKAREA", True);
    if (netWorkarea != None) {
        Atom actual_type;
        int actual_format;
        unsigned long nitems, bytes_after;
        long *data = NULL;
        if (XGetWindowProperty(g_x11_state.display, root, netWorkarea, 0, 4, False, XA_CARDINAL,
                               &actual_type, &actual_format, &nitems, &bytes_after, (unsigned char**)&data) == Success && data && nitems >= 4) {
            // data layout: x, y, width, height
            work_x = data[0];
            work_y = data[1];
            work_w = data[2];
            work_h = data[3];
            XFree(data);
        } else {
            // fallback to full screen
            work_x = 0;
            work_y = 0;
            work_w = DisplayWidth(g_x11_state.display, screen);
            work_h = DisplayHeight(g_x11_state.display, screen);
        }
    } else {
        work_x = 0;
        work_y = 0;
        work_w = DisplayWidth(g_x11_state.display, screen);
        work_h = DisplayHeight(g_x11_state.display, screen);
    }

    // Compute initial position at cursor + 640 Y offset and clamp inside work area
    Window returned_root, returned_child;
    int root_x, root_y, win_x, win_y;
    unsigned int mask;
    XQueryPointer(g_x11_state.display, root, &returned_root, &returned_child,
                  &root_x, &root_y, &win_x, &win_y, &mask);

    int createX = root_x;
    int createY = root_y - 640;  // Changed to -640 (above cursor)

    if (createX + g_x11_state.windowWidth > work_x + work_w) createX = work_x + work_w - g_x11_state.windowWidth;
    if (createY + g_x11_state.windowHeight > work_y + work_h) createY = work_y + work_h - g_x11_state.windowHeight;
    if (createX < work_x) createX = work_x;
    if (createY < work_y) createY = work_y;

    // Create an override-redirect (borderless) window positioned as computed
    XSetWindowAttributes swa;
    swa.override_redirect = True;
    swa.background_pixel = WhitePixel(g_x11_state.display, screen);
    swa.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | KeyPressMask | StructureNotifyMask | FocusChangeMask;

    g_x11_state.window = XCreateWindow(g_x11_state.display, root, createX, createY,
                                       g_x11_state.windowWidth, g_x11_state.windowHeight, 1,
                                       DefaultDepth(g_x11_state.display, screen), InputOutput,
                                       DefaultVisual(g_x11_state.display, screen),
                                       CWOverrideRedirect | CWBackPixel | CWEventMask, &swa);

    // Set window title
    XStoreName(g_x11_state.display, g_x11_state.window, "Better-Toolbar");

    // Create GC
    g_x11_state.gc = XCreateGC(g_x11_state.display, g_x11_state.window, 0, NULL);
    XSetBackground(g_x11_state.display, g_x11_state.gc, 0xFFFFFF);
    XSetForeground(g_x11_state.display, g_x11_state.gc, 0x000000);

    // Initialize directory path
    g_x11_state.filterStart = 1;
    
    if (argc >= 2) {
        char candidatePath[MAX_PATH_LEN];
        
        // Copy the argument
        strncpy(candidatePath, argv[1], MAX_PATH_LEN - 1);
        candidatePath[MAX_PATH_LEN - 1] = '\0';
        remove_trailing_slash(candidatePath);

        // Check if it's a directory
        if (is_directory(candidatePath)) {
            if (set_cur_dir(candidatePath)) {
                getcwd(g_x11_state.dirpath, MAX_PATH_LEN);
                remove_trailing_slash(g_x11_state.dirpath);
                g_x11_state.filterStart = 2;
            }
        }
    }
    
    // If not set, use current directory
    if (strlen(g_x11_state.dirpath) == 0) {
        getcwd(g_x11_state.dirpath, MAX_PATH_LEN);
        remove_trailing_slash(g_x11_state.dirpath);
    }
    
    // Scan directory
    g_x11_state.fileCount = scan_directory(g_x11_state.dirpath, g_x11_state.files, argc, argv, g_x11_state.filterStart);
    
    // Map window
    XMapWindow(g_x11_state.display, g_x11_state.window);
    XFlush(g_x11_state.display);
    
    // Event loop
    XEvent event;
    g_x11_state.quitFlag = 0;
    g_x11_state.buttonPressed = 0;
    
    while (!g_x11_state.quitFlag) {
        if (XPending(g_x11_state.display)) {
            XNextEvent(g_x11_state.display, &event);
            
            switch (event.type) {
                case Expose:
                    if (event.xexpose.count == 0) {
                        // refresh window content
                        draw_window();
                    }
                    break;
                
                case ButtonPress:
                    if (event.xbutton.button == 4) {
                        handle_mouse_scroll(-1);
                    } else if (event.xbutton.button == 5) {
                        handle_mouse_scroll(1);
                    } else if (event.xbutton.button == 1) {
                        handle_mouse_press(event.xbutton.x, event.xbutton.y);
                    }
                    break;
                
                case ButtonRelease:
                    if (event.xbutton.button == 1) {
                        handle_mouse_release(event.xbutton.x, event.xbutton.y);
                    }
                    break;
                
                case MotionNotify:
                    handle_mouse_move(event.xmotion.x, event.xmotion.y);
                    break;
                
                case ConfigureNotify:
                    g_x11_state.windowWidth = event.xconfigure.width;
                    g_x11_state.windowHeight = event.xconfigure.height;
                    draw_window();
                    break;
                
                case KeyPress:
                    if (event.xkey.keycode == 9) { // Escape key
                        g_x11_state.quitFlag = 1;
                    }
                    break;

                case FocusOut:
                    // quit when window loses focus/activation (behavior requested)
                    g_x11_state.quitFlag = 1;
                    break;
            }
        } else {
            usleep(10000); // sleep for 10ms to reduce CPU usage
        }
    }
    
    // Cleanup
    cleanup_x11();
    return 0;
}

int main(int argc, char *argv[]) {
    int result = 0;

    if (IS_CLI() == 0) {
        result = main_cli_function(argc, argv);
    } else {
        result = main_gui_function(argc, argv);
    }

    return result;
}

#endif  // !_WIN32
