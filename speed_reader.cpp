// One-word-at-a-time reader for Windows (.txt and .pdf via Poppler's pdftotext).
#include <windows.h>
#include <commdlg.h>
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

constexpr UINT_PTR WORD_TIMER_ID = 1, PREVIEW_TIMER_ID = 2;
constexpr int DEFAULT_DELAY = 300, STEP = 50, MIN_DELAY = 50;

std::vector<std::wstring> words;
size_t currentWord = 0;
int delayMs = DEFAULT_DELAY;
bool paused = false;
bool showPreviews = true;
bool showSideOverview = true;
bool fullscreenMode = true;
COLORREF displayColor = RGB(255, 255, 255);
int fontSizeAdjustment = 0;
std::wstring currentFileName;
HFONT normalFont, boldFont;
bool previewAnimating = false;
DWORD previewStartedAt = 0;
int previewDurationMs = 120;
std::wstring previousUpcoming, previousRead;

std::wstring Utf8ToWide(const std::string& input) {
    if (input.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, input.data(), (int)input.size(), nullptr, 0);
    if (!len) return L"";
    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, input.data(), (int)input.size(), result.data(), len);
    return result;
}

std::vector<std::wstring> SplitWords(const std::wstring& text) {
    std::wistringstream stream(text);
    std::vector<std::wstring> result;
    for (std::wstring word; stream >> word;) result.push_back(word);
    return result;
}

std::wstring JoinWordRange(size_t first, size_t after) {
    std::wstring result;
    for (size_t i = first; i < after; ++i) {
        if (!result.empty()) result += L" ";
        result += words[i];
    }
    return result;
}

void DrawPreview(HDC dc, const RECT& client, const std::wstring& text, int y, int xOffset, COLORREF color) {
    if (text.empty()) return;
    HFONT previewFont = CreateFontW(-24, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    DEFAULT_PITCH, L"Arial");
    HGDIOBJ previousFont = SelectObject(dc, previewFont);
    SetTextColor(dc, color);
    RECT line{32 + xOffset, y, client.right - 32 + xOffset, y + 40};
    DrawTextW(dc, text.c_str(), (int)text.size(), &line,
              DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, previousFont);
    DeleteObject(previewFont);
}

void DrawSideOverview(HDC dc, const RECT& client) {
    int panelLeft = client.right * 72 / 100;
    RECT panel{panelLeft, 64, client.right - 20, client.bottom - 20};
    FillRect(dc, &panel, (HBRUSH)GetStockObject(BLACK_BRUSH));

    HPEN divider = CreatePen(PS_SOLID, 1, RGB(75, 75, 75));
    HGDIOBJ oldPen = SelectObject(dc, divider);
    MoveToEx(dc, panelLeft, 64, nullptr); LineTo(dc, panelLeft, client.bottom - 20);
    SelectObject(dc, oldPen); DeleteObject(divider);

    HFONT titleFont = CreateFontW(-16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                  OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH, L"Arial");
    HGDIOBJ oldFont = SelectObject(dc, titleFont);
    SetTextColor(dc, RGB(170, 170, 170));
    size_t position = std::min(currentWord + 1, words.size());
    int percent = words.empty() ? 0 : int(position * 100 / words.size());
    std::wstring heading = L"TEXT OVERVIEW  —  " + std::to_wstring(percent) + L"%";
    TextOutW(dc, panelLeft + 22, 82, heading.c_str(), (int)heading.size());
    SelectObject(dc, oldFont); DeleteObject(titleFont);

    int trackX = panelLeft + 18, trackTop = 116, trackBottom = client.bottom - 32;
    HPEN trackPen = CreatePen(PS_SOLID, 1, RGB(80, 80, 80));
    oldPen = SelectObject(dc, trackPen);
    MoveToEx(dc, trackX, trackTop, nullptr); LineTo(dc, trackX, trackBottom);
    int markerY = trackTop + (trackBottom - trackTop) * percent / 100;
    HPEN markerPen = CreatePen(PS_SOLID, 3, RGB(180, 180, 180));
    SelectObject(dc, markerPen);
    MoveToEx(dc, trackX - 5, markerY, nullptr); LineTo(dc, trackX + 5, markerY);
    SelectObject(dc, oldPen); DeleteObject(trackPen); DeleteObject(markerPen);

    size_t first = currentWord > 90 ? currentWord - 90 : 0;
    size_t after = std::min(words.size(), currentWord + 180);
    std::wstring overview = JoinWordRange(first, after);
    HFONT textFont = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH, L"Arial");
    oldFont = SelectObject(dc, textFont);
    SetTextColor(dc, RGB(105, 105, 105));
    RECT textArea{panelLeft + 38, 116, client.right - 28, client.bottom - 32};
    DrawTextW(dc, overview.c_str(), (int)overview.size(), &textArea, DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, oldFont); DeleteObject(textFont);
}

void DrawFileFooter(HDC dc, const RECT& client) {
    if (currentFileName.empty()) return;
    HFONT footerFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                   OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                   DEFAULT_PITCH, L"Arial");
    HGDIOBJ oldFont = SelectObject(dc, footerFont);
    SetTextColor(dc, RGB(125, 125, 125));
    int right = showSideOverview ? client.right * 72 / 100 - 12 : client.right - 12;
    RECT footer{12, client.bottom - 32, right, client.bottom - 8};
    std::wstring label = L"File: " + currentFileName;
    DrawTextW(dc, label.c_str(), (int)label.size(), &footer,
              DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, oldFont); DeleteObject(footerFont);
}

std::wstring ReadTextFile(const std::wstring& path) {
    std::ifstream file(path.c_str(), std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(file)), {});
    // UTF-8 text is expected. Remove its optional byte-order mark.
    if (bytes.size() >= 3 && (unsigned char)bytes[0] == 0xEF &&
        (unsigned char)bytes[1] == 0xBB && (unsigned char)bytes[2] == 0xBF)
        bytes.erase(0, 3);
    return Utf8ToWide(bytes);
}

std::wstring ConvertWithCalibre(const std::wstring& path);

// Poppler's pdftotext must be installed and available on PATH for PDF support.
std::wstring ReadPdf(const std::wstring& path) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) return ConvertWithCalibre(path);
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
    std::wstring command = L"pdftotext.exe -layout \"" + path + L"\" -";
    std::vector<wchar_t> commandBuffer(command.begin(), command.end());
    commandBuffer.push_back(L'\0');
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    PROCESS_INFORMATION process{};
    bool started = CreateProcessW(nullptr, commandBuffer.data(), nullptr, nullptr, TRUE,
                                  CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    CloseHandle(writePipe);
    if (!started) { CloseHandle(readPipe); return ConvertWithCalibre(path); }
    std::string output;
    char buffer[4096]; DWORD count;
    while (ReadFile(readPipe, buffer, sizeof(buffer), &count, nullptr) && count)
        output.append(buffer, count);
    CloseHandle(readPipe);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1; GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    std::wstring text = exitCode == 0 ? Utf8ToWide(output) : L"";
    return text.empty() ? ConvertWithCalibre(path) : text;
}

bool RunHidden(const std::wstring& command) {
    std::vector<wchar_t> commandBuffer(command.begin(), command.end());
    commandBuffer.push_back(L'\0');
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, commandBuffer.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &startup, &process)) return false;
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1; GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    return exitCode == 0;
}

// Calibre's ebook-convert turns EPUB or PDF content into a single UTF-8 text file.
std::wstring ConvertWithCalibre(const std::wstring& path) {
    wchar_t tempFolder[MAX_PATH]{}, tempName[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, tempFolder) || !GetTempFileNameW(tempFolder, L"sr", 0, tempName)) return L"";
    std::wstring output = std::wstring(tempName) + L".txt";
    DeleteFileW(tempName);
    // Use the standard Calibre install location first, then PATH for portable installs.
    std::wstring converter = L"C:\\Program Files\\Calibre2\\ebook-convert.exe";
    if (GetFileAttributesW(converter.c_str()) == INVALID_FILE_ATTRIBUTES) converter = L"ebook-convert.exe";
    bool converted = RunHidden(L"\"" + converter + L"\" \"" + path + L"\" \"" + output + L"\"");
    std::wstring text = converted ? ReadTextFile(output) : L"";
    DeleteFileW(output.c_str());
    return text;
}

void SetFonts(HWND window) {
    RECT rect; GetClientRect(window, &rect);
    int baseHeight = std::clamp(static_cast<int>((rect.bottom - rect.top) / 10), 30, 80);
    int height = std::clamp(baseHeight + fontSizeAdjustment, 20, 180);
    DeleteObject(normalFont); DeleteObject(boldFont);
    normalFont = CreateFontW(-height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH, L"Arial");
    boldFont = CreateFontW(-height, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH, L"Arial");
}

void Draw(HWND window, HDC dc) {
    RECT rect; GetClientRect(window, &rect);
    FillRect(dc, &rect, (HBRUSH)GetStockObject(BLACK_BRUSH));
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255));
    HFONT legendFont = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                   OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                   DEFAULT_PITCH, L"Arial");
    HGDIOBJ previousFont = SelectObject(dc, legendFont);
    SetTextColor(dc, RGB(175, 175, 175));
    std::wstring legend = L"Speed: " + std::to_wstring(delayMs) + L" ms   |   1: fullscreen   |   2: windowed   |   3: overview on/off   |   4: larger   |   5: smaller   |   P: previews on/off   |   Space: pause/resume   |   Esc: exit";
    std::wstring colorLegend = L"Text color:  R: red   W: white   G: green   B: blue   Y: yellow   M: magenta   |   ←: slower   →: faster   ↑: back 12   ↓: forward 20";
    SIZE legendSize{};
    GetTextExtentPoint32W(dc, legend.c_str(), (int)legend.size(), &legendSize);
    TextOutW(dc, std::max(16L, (rect.right - legendSize.cx) / 2), 16, legend.c_str(), (int)legend.size());
    GetTextExtentPoint32W(dc, colorLegend.c_str(), (int)colorLegend.size(), &legendSize);
    TextOutW(dc, std::max(16L, (rect.right - legendSize.cx) / 2), 38, colorLegend.c_str(), (int)colorLegend.size());
    SelectObject(dc, previousFont);
    DeleteObject(legendFont);
    SetTextColor(dc, displayColor);
    int centerY = rect.bottom / 2;
    // The upcoming context is above the focal word; already-read context is below it.
    std::wstring upcoming = JoinWordRange(std::min(words.size(), currentWord + 1),
                                           std::min(words.size(), currentWord + 11));
    std::wstring alreadyRead = JoinWordRange(currentWord > 10 ? currentWord - 10 : 0,
                                              std::min(currentWord, words.size()));
    if (showPreviews && previewAnimating) {
        float progress = std::min(1.0f, float(GetTickCount() - previewStartedAt) / previewDurationMs);
        constexpr int TRAVEL_PX = 120;
        // Old lines slide left, while replacement lines enter from the right.
        DrawPreview(dc, rect, previousUpcoming, centerY - 90, -int(progress * TRAVEL_PX), RGB(75, 75, 75));
        DrawPreview(dc, rect, upcoming, centerY - 90, int((1.0f - progress) * TRAVEL_PX), RGB(115, 115, 115));
        DrawPreview(dc, rect, previousRead, centerY + 58, -int(progress * TRAVEL_PX), RGB(75, 75, 75));
        DrawPreview(dc, rect, alreadyRead, centerY + 58, int((1.0f - progress) * TRAVEL_PX), RGB(115, 115, 115));
    } else if (showPreviews) {
        DrawPreview(dc, rect, upcoming, centerY - 90, 0, RGB(115, 115, 115));
        DrawPreview(dc, rect, alreadyRead, centerY + 58, 0, RGB(115, 115, 115));
    }
    if (showSideOverview) DrawSideOverview(dc, rect);
    DrawFileFooter(dc, rect);
    SetTextColor(dc, displayColor);
    if (currentWord >= words.size()) {
        SelectObject(dc, boldFont); const wchar_t* done = L"Finished"; SIZE s;
        GetTextExtentPoint32W(dc, done, 8, &s);
        TextOutW(dc, (rect.right - s.cx) / 2, (rect.bottom - s.cy) / 2, done, 8);
        return;
    }
    const auto& word = words[currentWord];
    size_t focus = word.size() / 2;
    std::wstring left = word.substr(0, focus), middle = word.substr(focus, 1), right = word.substr(focus + 1);
    SIZE leftSize{}, middleSize{};
    SelectObject(dc, normalFont);
    GetTextExtentPoint32W(dc, left.c_str(), (int)left.size(), &leftSize);
    SelectObject(dc, boldFont);
    GetTextExtentPoint32W(dc, middle.c_str(), 1, &middleSize);
    int y = (rect.bottom - middleSize.cy) / 2;
    constexpr int BOX_PADDING_X = 12, BOX_PADDING_Y = 8;
    int middleX = rect.right / 2 - middleSize.cx / 2;
    RECT focusBox{middleX - BOX_PADDING_X, y - BOX_PADDING_Y,
                  middleX + middleSize.cx + BOX_PADDING_X, y + middleSize.cy + BOX_PADDING_Y};
    // The rectangle is explicitly filled black so the focused letter remains black-backgrounded.
    HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(145, 145, 145));
    HBRUSH blackBrush = CreateSolidBrush(RGB(0, 0, 0));
    HGDIOBJ oldPen = SelectObject(dc, borderPen);
    HGDIOBJ oldBrush = SelectObject(dc, blackBrush);
    Rectangle(dc, focusBox.left, focusBox.top, focusBox.right, focusBox.bottom);
    SelectObject(dc, oldPen); SelectObject(dc, oldBrush);
    DeleteObject(borderPen); DeleteObject(blackBrush);
    int leftX = focusBox.left - leftSize.cx;
    SelectObject(dc, normalFont);
    TextOutW(dc, leftX, y, left.c_str(), (int)left.size());
    SelectObject(dc, boldFont);
    TextOutW(dc, middleX, y, middle.c_str(), 1);
    SelectObject(dc, normalFont);
    TextOutW(dc, focusBox.right, y, right.c_str(), (int)right.size());
}

void SetViewMode(HWND window, bool fullscreen) {
    fullscreenMode = fullscreen;
    if (fullscreen) {
        SetWindowLongPtrW(window, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(window, HWND_TOP, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
        RECT workArea{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
        int width = std::min(1200L, workArea.right - workArea.left);
        int height = std::min(800L, workArea.bottom - workArea.top);
        int left = workArea.left + ((workArea.right - workArea.left) - width) / 2;
        int top = workArea.top + ((workArea.bottom - workArea.top) - height) / 2;
        SetWindowLongPtrW(window, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPos(window, HWND_NOTOPMOST, left, top, width, height, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_ERASEBKGND:
        return 1; // WM_PAINT draws the complete frame into an off-screen buffer.
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC screen = BeginPaint(window, &ps);
        RECT client; GetClientRect(window, &client);
        HDC buffer = CreateCompatibleDC(screen);
        HBITMAP bitmap = CreateCompatibleBitmap(screen, client.right, client.bottom);
        HGDIOBJ oldBitmap = SelectObject(buffer, bitmap);
        Draw(window, buffer);
        BitBlt(screen, 0, 0, client.right, client.bottom, buffer, 0, 0, SRCCOPY);
        SelectObject(buffer, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(buffer);
        EndPaint(window, &ps);
        return 0;
    }
    case WM_SIZE: SetFonts(window); InvalidateRect(window, nullptr, TRUE); return 0;
    case WM_TIMER:
        if (wParam == WORD_TIMER_ID && !paused && currentWord < words.size()) {
            previousUpcoming = JoinWordRange(std::min(words.size(), currentWord + 1), std::min(words.size(), currentWord + 11));
            previousRead = JoinWordRange(currentWord > 10 ? currentWord - 10 : 0, currentWord);
            ++currentWord;
            previewDurationMs = std::clamp(delayMs / 2, 30, 150);
            previewStartedAt = GetTickCount();
            previewAnimating = true;
            SetTimer(window, PREVIEW_TIMER_ID, 16, nullptr); // repaint at roughly 60 frames per second
            InvalidateRect(window, nullptr, TRUE);
        } else if (wParam == PREVIEW_TIMER_ID) {
            if (GetTickCount() - previewStartedAt >= (DWORD)previewDurationMs) {
                previewAnimating = false;
                KillTimer(window, PREVIEW_TIMER_ID);
            }
            InvalidateRect(window, nullptr, TRUE);
        }
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_SPACE) { paused = !paused; InvalidateRect(window, nullptr, TRUE); }
        else if (wParam == VK_LEFT) { delayMs += STEP; KillTimer(window, WORD_TIMER_ID); SetTimer(window, WORD_TIMER_ID, delayMs, nullptr); InvalidateRect(window, nullptr, TRUE); }
        else if (wParam == VK_RIGHT) { delayMs = std::max(MIN_DELAY, delayMs - STEP); KillTimer(window, WORD_TIMER_ID); SetTimer(window, WORD_TIMER_ID, delayMs, nullptr); InvalidateRect(window, nullptr, TRUE); }
        else if (wParam == VK_UP) { previewAnimating = false; KillTimer(window, PREVIEW_TIMER_ID); currentWord = currentWord > 12 ? currentWord - 12 : 0; InvalidateRect(window, nullptr, TRUE); }
        else if (wParam == VK_DOWN) { previewAnimating = false; KillTimer(window, PREVIEW_TIMER_ID); currentWord = std::min(words.size(), currentWord + 20); InvalidateRect(window, nullptr, TRUE); }
        else if (wParam == 'P') { showPreviews = !showPreviews; InvalidateRect(window, nullptr, TRUE); }
        else if (wParam == '3') { showSideOverview = !showSideOverview; InvalidateRect(window, nullptr, TRUE); }
        else if (wParam == '4') { fontSizeAdjustment = std::min(100, fontSizeAdjustment + 4); SetFonts(window); InvalidateRect(window, nullptr, TRUE); }
        else if (wParam == '5') { fontSizeAdjustment = std::max(-50, fontSizeAdjustment - 4); SetFonts(window); InvalidateRect(window, nullptr, TRUE); }
        else if (wParam == 'R') { displayColor = RGB(255, 70, 70); InvalidateRect(window, nullptr, TRUE); }
        else if (wParam == 'W') { displayColor = RGB(255, 255, 255); InvalidateRect(window, nullptr, TRUE); }
        else if (wParam == 'G') { displayColor = RGB(80, 235, 100); InvalidateRect(window, nullptr, TRUE); }
        else if (wParam == 'B') { displayColor = RGB(80, 160, 255); InvalidateRect(window, nullptr, TRUE); }
        else if (wParam == 'Y') { displayColor = RGB(255, 225, 55); InvalidateRect(window, nullptr, TRUE); }
        else if (wParam == 'M') { displayColor = RGB(245, 80, 245); InvalidateRect(window, nullptr, TRUE); }
        else if (wParam == '1') SetViewMode(window, true);
        else if (wParam == '2') SetViewMode(window, false);
        else if (wParam == VK_ESCAPE) DestroyWindow(window);
        return 0;
    case WM_DESTROY: DeleteObject(normalFont); DeleteObject(boldFont); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    wchar_t path[MAX_PATH]{};
    OPENFILENAMEW picker{sizeof(picker)};
    picker.lpstrFile = path; picker.nMaxFile = MAX_PATH;
    picker.lpstrFilter = L"Supported files\0*.txt;*.pdf;*.epub\0Text files\0*.txt\0PDF files\0*.pdf\0EPUB files\0*.epub\0\0";
    picker.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&picker)) return 0;
    std::wstring filePath(path), text;
    size_t filenameStart = filePath.find_last_of(L"\\/");
    currentFileName = filenameStart == std::wstring::npos ? filePath : filePath.substr(filenameStart + 1);
    bool isPdf = filePath.size() >= 4 && _wcsicmp(filePath.c_str() + filePath.size() - 4, L".pdf") == 0;
    bool isEpub = filePath.size() >= 5 && _wcsicmp(filePath.c_str() + filePath.size() - 5, L".epub") == 0;
    text = isPdf ? ReadPdf(filePath) : (isEpub ? ConvertWithCalibre(filePath) : ReadTextFile(filePath));
    words = SplitWords(text);
    if (words.empty()) {
        const wchar_t* error = isPdf ? L"Could not read this PDF. The file may be scanned/image-only or protected; use an OCR/text-based PDF."
            : isEpub ? L"Could not read this EPUB. Install Calibre (ebook-convert.exe) and add it to PATH."
            : L"No readable words were found.";
        MessageBoxW(nullptr, error, L"Speed Reader", MB_ICONERROR);
        return 1;
    }
    const wchar_t CLASS_NAME[] = L"SpeedReaderWindow";
    WNDCLASSW wc{}; wc.hInstance = instance; wc.lpszClassName = CLASS_NAME; wc.lpfnWndProc = WindowProc; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    HWND window = CreateWindowExW(0, CLASS_NAME, L"Speed Reader", WS_POPUP | WS_VISIBLE,
                                   0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), nullptr, nullptr, instance, nullptr);
    SetFonts(window); SetTimer(window, WORD_TIMER_ID, delayMs, nullptr);
    MSG message; while (GetMessageW(&message, nullptr, 0, 0)) { TranslateMessage(&message); DispatchMessageW(&message); }
    return 0;
}
