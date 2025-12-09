#define UNICODE
#define _UNICODE

#include <windows.h>
#include <shellapi.h>
#include <wchar.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <direct.h> // For _chdir
#include <unistd.h> // For access()

#define MAX_FILES 2048
#define MAX_PATH_LEN 32767  // Max Windows path with \\?\ prefix
#define BUTTON_HEIGHT 40
#define BUTTON_WIDTH 260
#define BUTTON_START_Y 110
#define BUTTON_START_ID 2000

// Global variables for GUI
typedef struct {
    char dirpath[MAX_PATH_LEN];
    char *files[MAX_FILES];
    int fileCount;
    int filterStart;
    HWND fileButtons[MAX_FILES];
    HWND hwndMain;
    HWND hwndScrollbar;
    int scrollPos;
    HFONT hGuiFont; // <--- ADD THIS
} AppState;

AppState g_state = {0};

int IS_CLI() {
    if (access("CLI_MODE", F_OK) == 0) {
        return 0;
    }
    return 1;
}

// Convert wchar_t to char
void wchar_to_char(const wchar_t *wstr, char *str, int str_size) {
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, str, str_size, NULL, NULL);
}

// Convert char to wchar_t
void char_to_wchar(const char *str, wchar_t *wstr, int wstr_size) {
    MultiByteToWideChar(CP_UTF8, 0, str, -1, wstr, wstr_size);
}

// Check if path is a directory
int is_directory(const char *path) {
    wchar_t wpath[MAX_PATH_LEN];
    char_to_wchar(path, wpath, MAX_PATH_LEN);
    DWORD attr = GetFileAttributesW(wpath);
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
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
    system("cls");
}

// Remove trailing slashes from a path
void remove_trailing_slash(char *path) {
    size_t len = strlen(path);
    
    // Remove all trailing slashes
    while (len > 0 && (path[len - 1] == '\\' || path[len - 1] == '/')) {
        path[len - 1] = '\0';
        len--;
    }
    
    // Don't remove trailing slash from root directories like "C:\"
    if (len > 0 && path[len - 1] == ':') {
        strcat(path, "\\");
    }
}

// Set current working directory
int set_cur_dir(const char *path) {
    wchar_t wpath[MAX_PATH_LEN];
    char_to_wchar(path, wpath, MAX_PATH_LEN);
    
    // Use _wchdir (Wide Char) instead of _chdir
    if (_wchdir(wpath) == 0) {
        return 1;
    }
    return 0;
}

// Scan directory and fill file list
int scan_directory(const char *dirpath, char *files[], int argc, char *argv[], int filterStart) {
    WIN32_FIND_DATAW fd;
    wchar_t searchPath[MAX_PATH_LEN];
    wchar_t wdirpath[MAX_PATH_LEN];
    
    char_to_wchar(dirpath, wdirpath, MAX_PATH_LEN);
    _snwprintf(searchPath, MAX_PATH_LEN, L"%s\\*", wdirpath);

    HANDLE hFind = FindFirstFileW(searchPath, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return 0;

    int count = 0;
    do {
        const wchar_t *wname = fd.cFileName;
        if (wcscmp(wname, L".") == 0 || wcscmp(wname, L"..") == 0) continue;

        char name[MAX_PATH_LEN];
        wchar_to_char(wname, name, MAX_PATH_LEN);

        if (matches_filters(name, argc, argv, filterStart) && count < MAX_FILES)
            files[count++] = _strdup(name);

    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
    return count;
}

// Print documentation
void print_documentation() {
    clear_console();
    printf("Better Toolbar CLI\n");
    printf("Navigate folders, open files, supports absolute paths.\n");
    printf("Usage:\n");
    printf("  better-toolbar.exe [folder] [filters...]\n\n");
    printf("Examples:\n");
    printf("  better-toolbar.exe C:\\Users\\Documents\n");
    printf("  better-toolbar.exe . .txt .pdf\n");
    printf("  better-toolbar.exe D:\\Projects .cpp .h\n");
}

// Main function
int main_cli_function(int argc, char *argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8); // Add this for input support

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
                wchar_t wdirpath[MAX_PATH_LEN];
                GetCurrentDirectoryW(MAX_PATH_LEN, wdirpath);
                wchar_to_char(wdirpath, dirpath, MAX_PATH_LEN);
                remove_trailing_slash(dirpath);
                filterStart = 2; // Filters start at argv[2]
            } else {
                // Failed to change directory
                printf("Error: Cannot access directory '%s'\n", candidatePath);
                // Use current directory as fallback
                wchar_t wdirpath[MAX_PATH_LEN];
                GetCurrentDirectoryW(MAX_PATH_LEN, wdirpath);
                wchar_to_char(wdirpath, dirpath, MAX_PATH_LEN);
                remove_trailing_slash(dirpath);
                filterStart = 1; // Treat argv[1] as filter
            }
        } else {
            // Not a directory - use current dir, treat argv[1] as filter
            wchar_t wdirpath[MAX_PATH_LEN];
            GetCurrentDirectoryW(MAX_PATH_LEN, wdirpath);
            wchar_to_char(wdirpath, dirpath, MAX_PATH_LEN);
            remove_trailing_slash(dirpath);
            filterStart = 1;
        }
    } else {
        // No arguments - use current directory
        wchar_t wdirpath[MAX_PATH_LEN];
        GetCurrentDirectoryW(MAX_PATH_LEN, wdirpath);
        wchar_to_char(wdirpath, dirpath, MAX_PATH_LEN);
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

        if (_stricmp(input, "up") == 0) {
            char tempPath[MAX_PATH_LEN];
            strcpy(tempPath, dirpath);
            
            char *lastSlash = strrchr(tempPath, '\\');
            if (!lastSlash) lastSlash = strrchr(tempPath, '/');
            
            if (lastSlash) {
                *lastSlash = '\0';
                
                // If we're at a drive root, stay there
                if (strlen(tempPath) > 0 && tempPath[strlen(tempPath) - 1] == ':') {
                    strcat(tempPath, "\\");
                }
                
                // Try to change to parent directory
                if (set_cur_dir(tempPath)) {
                    wchar_t wdirpath[MAX_PATH_LEN];
                    GetCurrentDirectoryW(MAX_PATH_LEN, wdirpath);
                    wchar_to_char(wdirpath, dirpath, MAX_PATH_LEN);
                    remove_trailing_slash(dirpath);
                }
            }
            // If no slash found, we're already at root - stay here
            continue;
        }

        if (!is_number(input)) {
            printf("Invalid input!\n");
            Sleep(1000);
            continue;
        }

        int index = atoi(input);
        if (index < 0 || index >= fileCount) {
            printf("Index out of range!\n");
            Sleep(1000);
            continue;
        }

        char fullPath[MAX_PATH_LEN];
        snprintf(fullPath, MAX_PATH_LEN, "%s\\%s", dirpath, files[index]);

        if (is_directory(fullPath)) {
            // Change scanning directory to subdirectory
            if (set_cur_dir(fullPath)) {
                wchar_t wdirpath[MAX_PATH_LEN];
                GetCurrentDirectoryW(MAX_PATH_LEN, wdirpath);
                wchar_to_char(wdirpath, dirpath, MAX_PATH_LEN);
                remove_trailing_slash(dirpath);
            }
            continue;
        }

        // Open file with default application
        wchar_t wfullPath[MAX_PATH_LEN];
        char_to_wchar(fullPath, wfullPath, MAX_PATH_LEN);
        ShellExecuteW(NULL, L"open", wfullPath, NULL, NULL, SW_SHOWNORMAL);
    }

    // Final cleanup
    for (int i = 0; i < fileCount; i++) free(files[i]);
    printf("Exiting.\n");
    return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

HWND add_button(const wchar_t *label, HWND parent, int x, int y, int width, int height, int id)
{
    return CreateWindowW(
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
}

// Destroy all file buttons
void destroy_file_buttons() {
    for (int i = 0; i < g_state.fileCount; i++) {
        if (g_state.fileButtons[i]) {
            DestroyWindow(g_state.fileButtons[i]);
            g_state.fileButtons[i] = NULL;
        }
    }
}

// Update scrollbar range based on content
void update_scrollbar() {
    if (!g_state.hwndScrollbar) return;
    
    RECT clientRect;
    GetClientRect(g_state.hwndMain, &clientRect);
    int clientHeight = clientRect.bottom - BUTTON_START_Y;
    
    int totalContentHeight = g_state.fileCount * BUTTON_HEIGHT;
    int maxScroll = totalContentHeight - clientHeight;
    
    if (maxScroll < 0) maxScroll = 0;
    
    SCROLLINFO si = {0};
    si.cbSize = sizeof(SCROLLINFO);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = totalContentHeight;
    si.nPage = clientHeight;
    si.nPos = g_state.scrollPos;
    
    SetScrollInfo(g_state.hwndScrollbar, SB_CTL, &si, TRUE);
}

// Create file buttons dynamically
void create_file_buttons(int argc, char *argv[]) {
    // Destroy old buttons first
    destroy_file_buttons();
    
    // Free old file list
    for (int i = 0; i < g_state.fileCount; i++) {
        if (g_state.files[i]) {
            free(g_state.files[i]);
            g_state.files[i] = NULL;
        }
    }
    
    // Scan directory
    g_state.fileCount = scan_directory(g_state.dirpath, g_state.files, argc, argv, g_state.filterStart);
    
    // Create new buttons for each file
    for (int i = 0; i < g_state.fileCount; i++) {
        wchar_t wlabel[MAX_PATH_LEN];
        char_to_wchar(g_state.files[i], wlabel, MAX_PATH_LEN);
        
        int yPos = BUTTON_START_Y + (i * BUTTON_HEIGHT) - g_state.scrollPos;
        
        g_state.fileButtons[i] = CreateWindowW(
            L"BUTTON",
            wlabel,
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            10, yPos,
            BUTTON_WIDTH, BUTTON_HEIGHT - 5,
            g_state.hwndMain,
            (HMENU)(intptr_t)(BUTTON_START_ID + i),
            GetModuleHandle(NULL),
            NULL
        );

        SendMessage(g_state.fileButtons[i], WM_SETFONT, (WPARAM)g_state.hGuiFont, TRUE);
    }
    
    // Update scrollbar
    update_scrollbar();
    
    // Redraw window
    InvalidateRect(g_state.hwndMain, NULL, TRUE);
}

// Reposition file buttons based on scroll position
void reposition_file_buttons() {
    for (int i = 0; i < g_state.fileCount; i++) {
        if (g_state.fileButtons[i]) {
            int yPos = BUTTON_START_Y + (i * BUTTON_HEIGHT) - g_state.scrollPos;
            SetWindowPos(g_state.fileButtons[i], NULL, 10, yPos, 0, 0, 
                        SWP_NOSIZE | SWP_NOZORDER);
        }
    }
}

// Handle file button click
void handle_file_button_click(int buttonIndex) {
    if (buttonIndex < 0 || buttonIndex >= g_state.fileCount) return;
    
    char fullPath[MAX_PATH_LEN];
    snprintf(fullPath, MAX_PATH_LEN, "%s\\%s", g_state.dirpath, g_state.files[buttonIndex]);
    
    if (is_directory(fullPath)) {
        // Navigate into directory
        if (set_cur_dir(fullPath)) {
            wchar_t wdirpath[MAX_PATH_LEN];
            GetCurrentDirectoryW(MAX_PATH_LEN, wdirpath);
            wchar_to_char(wdirpath, g_state.dirpath, MAX_PATH_LEN);
            remove_trailing_slash(g_state.dirpath);
            
            // Reset scroll position
            g_state.scrollPos = 0;
            
            // Refresh the file list
            extern int g_argc;
            extern char **g_argv;
            create_file_buttons(g_argc, g_argv);
        }
    } else {
        // Open file with default application
        wchar_t wfullPath[MAX_PATH_LEN];
        char_to_wchar(fullPath, wfullPath, MAX_PATH_LEN);
        ShellExecuteW(NULL, L"open", wfullPath, NULL, NULL, SW_SHOWNORMAL);
    }
}

// Handle "Up" button - go to parent directory
void handle_up_button(int argc, char *argv[]) {
    char tempPath[MAX_PATH_LEN];
    strcpy(tempPath, g_state.dirpath);
    
    char *lastSlash = strrchr(tempPath, '\\');
    if (!lastSlash) lastSlash = strrchr(tempPath, '/');
    
    if (lastSlash) {
        *lastSlash = '\0';
        
        // If we're at a drive root, stay there
        if (strlen(tempPath) > 0 && tempPath[strlen(tempPath) - 1] == ':') {
            strcat(tempPath, "\\");
        }
        
        // Try to change to parent directory
        if (set_cur_dir(tempPath)) {
            wchar_t wdirpath[MAX_PATH_LEN];
            GetCurrentDirectoryW(MAX_PATH_LEN, wdirpath);
            wchar_to_char(wdirpath, g_state.dirpath, MAX_PATH_LEN);
            remove_trailing_slash(g_state.dirpath);
            
            // Reset scroll position
            g_state.scrollPos = 0;
            
            // Refresh the file list
            create_file_buttons(argc, argv);
        }
    }
}

// Global argc/argv for GUI mode
int g_argc = 0;
char **g_argv = NULL;

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
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClass(&wc);

    wchar_t title[MAX_PATH_LEN];

    int windowWidth = 300;
    int windowHeight = 600;
    const int verticalOffset = 600; // Your desired fixed upward shift
    const int horizontalOffset = 10; // Small horizontal offset to avoid covering the cursor
    
    // 1. Get Mouse Position
    POINT pt;
    if (!GetCursorPos(&pt)) {
        // Fallback if getting cursor position fails
        pt.x = CW_USEDEFAULT;
        pt.y = CW_USEDEFAULT;
    }

    // 2. Get the usable desktop area (Working Area), excluding the taskbar
    RECT workArea;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);

    // 3. Calculate Initial Desired Position (including your fixed offset)
    int initialX = pt.x + horizontalOffset;
    // Initial Y position is the cursor's Y minus your fixed upward offset
    int initialY = pt.y - verticalOffset; 
    
    // 4. Clamp the Y position to ensure no overlap with taskbar or going off the top
    int finalY = initialY;
    
    // If the top edge goes above the top of the working area, pull it down
    if (finalY < workArea.top) {
        finalY = workArea.top;
    }
    
    // Calculate the maximum allowed top Y-coordinate to avoid taskbar overlap.
    // Since your window is 600px tall, the highest Y value it can have is:
    int yMax = workArea.bottom - windowHeight;
    
    // If the top edge is too far down (meaning the bottom edge is below the taskbar)
    // this check might only be necessary if the fixed offset calculation yields a result 
    // that is still too low, but keeping it is robust.
    if (finalY > yMax) {
        finalY = yMax;
    }


    // 5. Clamp the X position to ensure the window stays on screen
    int finalX = initialX;
    
    // If the right edge goes past the screen edge, pull it left
    int xMax = workArea.right - windowWidth;
    if (finalX > xMax) {
        finalX = xMax;
    }
    
    // If the left edge goes off the screen, pull it right
    if (finalX < workArea.left) {
        finalX = workArea.left;
    }

    g_state.hGuiFont = CreateFontW(
        19, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );

    char_to_wchar("Better-Toolbar", title, MAX_PATH_LEN);

    // Initialize directory path
    g_state.filterStart = 1;
    
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
                wchar_to_char(wdirpath, g_state.dirpath, MAX_PATH_LEN);
                char_to_wchar(g_state.dirpath, title, MAX_PATH_LEN);
                remove_trailing_slash(g_state.dirpath);
                g_state.filterStart = 2;
            }
        }
    }
    
    // If not set, use current directory
    if (strlen(g_state.dirpath) == 0) {
        wchar_t wdirpath[MAX_PATH_LEN];
        GetCurrentDirectoryW(MAX_PATH_LEN, wdirpath);
        wchar_to_char(wdirpath, g_state.dirpath, MAX_PATH_LEN);
        remove_trailing_slash(g_state.dirpath);
    }

    HWND hwnd = CreateWindowEx(
        0,
        CLASS_NAME,
        title,
        WS_POPUP | WS_VISIBLE | WS_VSCROLL, // Borderless style
        finalX, finalY, windowWidth, windowHeight, // Use clamped coordinates and variables
        NULL, NULL, hInstance, NULL
    );

    if (hwnd == NULL) {
        return 1;
    }

    g_state.hwndMain = hwnd;
    g_state.scrollPos = 0;

    // Create scrollbar
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    g_state.hwndScrollbar = CreateWindowW(
        L"SCROLLBAR",
        NULL,
        WS_CHILD | WS_VISIBLE | SBS_VERT,
        clientRect.right - 20, BUTTON_START_Y,
        20, clientRect.bottom - BUTTON_START_Y,
        hwnd,
        (HMENU)9999,
        hInstance,
        NULL
    );

    HWND hUp = add_button(L"⬆️", hwnd, 10, 10, 80, 40, 1001);
    HWND hRef = add_button(L"🔄️", hwnd, 100, 10, 80, 40, 1002);
    HWND hQuit = add_button(L"❌", hwnd, 190, 10, 80, 40, 1003);

    SendMessage(hUp, WM_SETFONT, (WPARAM)g_state.hGuiFont, TRUE);
    SendMessage(hRef, WM_SETFONT, (WPARAM)g_state.hGuiFont, TRUE);
    SendMessage(hQuit, WM_SETFONT, (WPARAM)g_state.hGuiFont, TRUE);

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
    for (int i = 0; i < g_state.fileCount; i++) {
        if (g_state.files[i]) free(g_state.files[i]);
    }

    return (int)msg.wParam;
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
    if (g_state.hGuiFont) DeleteObject(g_state.hGuiFont);
    LocalFree(argvW);
    for (int i = 0; i < argc; ++i) free(argv[i]);
    free(argv);

    return result;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg) {
        case WM_COMMAND: {
            int id = LOWORD(wParam);  // Button ID

            switch (id) {
                case 1001:  // Up button
                    handle_up_button(g_argc, g_argv);
                    return 0;

                case 1002:  // Refresh
                    g_state.scrollPos = 0;
                    create_file_buttons(g_argc, g_argv);
                    return 0;

                case 1003:  // Quit
                    PostQuitMessage(0);
                    return 0;
                
                default:
                    // Check if it's a file button
                    if (id >= BUTTON_START_ID && id < BUTTON_START_ID + MAX_FILES) {
                        int buttonIndex = id - BUTTON_START_ID;
                        handle_file_button_click(buttonIndex);
                    }
                    return 0;
            }
            break;
        }

        case WM_VSCROLL: {
            SCROLLINFO si = {0};
            si.cbSize = sizeof(SCROLLINFO);
            si.fMask = SIF_ALL;
            GetScrollInfo(g_state.hwndScrollbar, SB_CTL, &si);
            
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
            SetScrollInfo(g_state.hwndScrollbar, SB_CTL, &si, TRUE);
            GetScrollInfo(g_state.hwndScrollbar, SB_CTL, &si);
            
            if (si.nPos != oldPos) {
                g_state.scrollPos = si.nPos;
                reposition_file_buttons();
            }
            
            return 0;
        }

        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            SCROLLINFO si = {0};
            si.cbSize = sizeof(SCROLLINFO);
            si.fMask = SIF_ALL;
            GetScrollInfo(g_state.hwndScrollbar, SB_CTL, &si);
            
            int oldPos = si.nPos;
            si.nPos -= (delta / WHEEL_DELTA) * BUTTON_HEIGHT;
            
            si.fMask = SIF_POS;
            SetScrollInfo(g_state.hwndScrollbar, SB_CTL, &si, TRUE);
            GetScrollInfo(g_state.hwndScrollbar, SB_CTL, &si);
            
            if (si.nPos != oldPos) {
                g_state.scrollPos = si.nPos;
                reposition_file_buttons();
            }
            
            return 0;
        }

        case WM_SIZE: {
            // Resize scrollbar when window is resized
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            
            if (g_state.hwndScrollbar) {
                SetWindowPos(g_state.hwndScrollbar, NULL,
                            clientRect.right - 20, BUTTON_START_Y,
                            20, clientRect.bottom - BUTTON_START_Y,
                            SWP_NOZORDER);
                update_scrollbar();
            }
            return 0;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            // Select the modern font into the HDC
            SelectObject(hdc, g_state.hGuiFont);
            SetBkMode(hdc, TRANSPARENT); // Optional: makes text look cleaner

            // Display current directory
            wchar_t wdirpath[MAX_PATH_LEN];
            char_to_wchar(g_state.dirpath, wdirpath, MAX_PATH_LEN);
            TextOutW(hdc, 10, 60, wdirpath, wcslen(wdirpath));

            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}
