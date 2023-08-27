#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include "KeyCodes.h"
#include "resources.h"
#include "mmsystem.h"
#include "WinUser.h"

/* ---------------------------------data types */

#define IDT_GAME 42
#define IDT_SCORE 43
#define SCORECONST 26
#define MAXWIDTH 30
#define MAXHEIGHT 80
#define MAXSETS 3
#define PIECETYPES 7 /* number of different piece types. makes it easy to add new pieces if needed */
#define FREE 0
#define CURRENT 1
#define FIRSTPIECE 2
typedef struct
{
  int width;
  int height;
  int state[MAXHEIGHT][MAXWIDTH];
} tetris;
typedef int piece[4][4];
typedef piece piecetable[PIECETYPES][4]; /* 4 different 'rotation states' for each piece */
/*--------------------------------------------*/

typedef struct
{
  TCHAR name[20];
  DWORD score;
  DWORD lines;
  DWORD level;
} hiscore, hiscoretable[10];

/* Global variables.
   too many... */

HWND hMainWin, hPaintWin = NULL;
RECT cliRect;
HBRUSH bgBrush = NULL, bgBrush2 = NULL;
HFONT hMyFont;
SIZE bmSize[MAXSETS];

BYTE i;
int x;
int y;
BYTE piecescore = 23;
BYTE timelevel = 40;
BYTE currpiece;
BYTE nextpiece;
BYTE level = IDC_NOVICE;
BYTE initWidth = 10;
BYTE initHeight = 24;
BYTE mciBuf[128];
BYTE blockset = 0;
WORD xPixPerPiece;
WORD yPixPerPiece;
DWORD lines = 0;
DWORD score = 0;

BOOL levelchanged = FALSE;
BOOL playing = FALSE;
BOOL paused = FALSE;
BOOL midi;
BOOL soundon = TRUE;
BOOL preview = TRUE;

HBITMAP bitmaps[MAXSETS][PIECETYPES];
HBITMAP previews[PIECETYPES];
HBITMAP hBMLogo;

HFONT hFont;
COLORREF bgcolor1 = RGB(126, 126, 126);
COLORREF bgcolor2 = RGB(33, 132, 115);

hiscoretable hstable;
tetris myTetris;
TCHAR hiscorefile[] = "gditetris.hst";
TCHAR namebuffer[21];

#include "pieces.h"

void fatal_error_win(LPSTR str)
{
  TCHAR msg[80];
  DWORD er = GetLastError();
  sprintf(msg, "%s failed: GetLastError returned %ld\n",
          str, er);
  MessageBox(NULL, msg, "Fatal Error", MB_OK);
  ExitProcess(er);
}

HFONT SetFont()
{
  /*
    LOGFONT f;
    f.lfHeight=1;
    f.lfWidth=0;
    f.lfEscapement = 0;
    f.lfOrientation = 0;
    f.lfItalic=FALSE;
    f.lfUnderline=FALSE;
    f.lfStrikeOut=FALSE;
    f.lfOutPrecision = OUT_RASTER_PRECIS;
    f.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    f.lfQuality = DRAFT_QUALITY;
    f.lfCharSet=DEFAULT_CHARSET;
    f.lfWeight = FW_BLACK;
    f.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    strcpy(f.lfFaceName,"courier");
    return CreateFontIndirect(&f);
    */

  HANDLE hMyFont = INVALID_HANDLE_VALUE;       // Here, we will (hopefully) get our font handle
  HINSTANCE hInstance = GetModuleHandle(NULL); // Or could even be a DLL's HINSTANCE
  HRSRC hFntRes = FindResource(hInstance, MAKEINTRESOURCE(IDF_MYFONT), RT_FONT);
  if (hFntRes)
  {                                                     // If we have found the resource ...
    HGLOBAL hFntMem = LoadResource(hInstance, hFntRes); // Load it
    if (hFntMem != NULL)
    {
      void *FntData = LockResource(hFntMem); // Lock it into accessible memory
      DWORD nFonts = 0, len = SizeofResource(hInstance, hFntMem);
      hMyFont = AddFontMemResourceEx(FntData, len, NULL, &nFonts); // Fake install font!
    }
    LOGFONT MyFont = {16, 8, 0, 0, 400, FALSE, FALSE, FALSE, ANSI_CHARSET,
                      OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                      VARIABLE_PITCH | FF_SWISS, "Stencil"};
    return CreateFontIndirect(&MyFont);
  }
}

void ChooseBgColor()
{

  CHOOSECOLOR c;

  InitCommonControls();

  c.lStructSize = sizeof(CHOOSECOLOR);
  c.hwndOwner = hMainWin;
  c.rgbResult = bgcolor1;
  c.lpCustColors = (COLORREF *)malloc(16 * sizeof(COLORREF));
  c.Flags = CC_RGBINIT;
  if (ChooseColor(&c) != 0)
  {
    bgcolor1 = c.rgbResult;
    DeleteObject(bgBrush);
    bgBrush = CreateSolidBrush(bgcolor1);
  }
  free(c.lpCustColors);
}

BOOL isfulline(int *l, int j);
void DebugWrite(tetris *t, int ypos)
{
  int row, col;
  BOOL plsave = playing;
  FILE *debugfile = fopen("debug.txt", "w");
  playing = FALSE;
  if (debugfile)
  {
    fprintf(debugfile, "Debug param: %d\n\n", ypos);
    for (row = 0; row < t->height; row++)
    {
      if (isfulline(t->state[row], t->width))
        fprintf(debugfile, "line nr %d is full\n", row);
      for (col = 0; col < t->width; col++)
        fputc(t->state[t->height - 1 - row][col] + '0', debugfile);
      fprintf(debugfile, "\n");
    }
  }
  fclose(debugfile);
  playing = plsave;
}

WORD getWORD(FILE *f)
{
  return (WORD)(fgetc(f) | (fgetc(f) << 8));
}

void putWORD(WORD var, FILE *f)
{
  fputc((BYTE)(var & 0xFF), f);
  fputc((BYTE)((var & (0xFF << 8)) >> 8), f);
}

void putDWORD(DWORD var, FILE *f)
{
  putWORD((WORD)(var & 0xFFFF), f);
  putWORD((WORD)((var & (0xFFFF << 16)) >> 16), f);
}

DWORD getDWORD(FILE *f)
{
  return (DWORD)(getWORD(f) | (getWORD(f) << 16));
}

void Display_BMP(HDC hdc, HBITMAP bmp,
                 int x, int y, int width, int height)
{

  HDC hdcMem = CreateCompatibleDC(NULL);
  HBITMAP hbmT = SelectBitmap(hdcMem, bmp);
  BitBlt(hdc,
         x, y,
         width, height,
         hdcMem, 0, 0, SRCCOPY);
  SelectBitmap(hdcMem, hbmT);
  DeleteDC(hdcMem);
}

HKEY get_reg_key()
{
  HKEY hKey;

  /* Opens the RegKey, and if it doesn't exist, creates it. */
  LONG err = RegOpenKeyExA(HKEY_CURRENT_USER, "SoftWare", 0, KEY_CREATE_SUB_KEY, &hKey);
  CHAR msg[256];
  sprintf(msg, "RegOpenKeyExA Error %d ", err);
  if (err != ERROR_SUCCESS)
    fatal_error_win(msg);
  err = RegCreateKeyExA(hKey, "Jan Rancken Software", 0, NULL, 0x00000000L, KEY_CREATE_SUB_KEY, NULL, &hKey, NULL);
  if (err != ERROR_SUCCESS)
  {
    sprintf(msg, "RegCreateKeyExA Error %d ", err);
    fatal_error_win(msg);
  }

  if (RegCreateKeyExA(hKey, "GDI Tetris 1.0", 0, NULL, 0x00000001L, KEY_ALL_ACCESS, NULL, &hKey, NULL) != ERROR_SUCCESS)
    fatal_error_win("RegCreateKey 2");

  return hKey;
}

void read_config()
{
  HKEY hKey = get_reg_key();
  DWORD size = sizeof(DWORD);
  RegQueryValueExA(hKey, "preview", NULL, NULL, (LPBYTE)&preview, (LPDWORD)&size);
  RegQueryValueExA(hKey, "preview", NULL, NULL, (LPBYTE)&soundon, (LPDWORD)&size);
  RegQueryValueExA(hKey, "preview", NULL, NULL, (LPBYTE)&blockset, (LPDWORD)&size);
  RegQueryValueExA(hKey, "preview", NULL, NULL, (LPBYTE)&bgcolor1, (LPDWORD)&size);
}

void save_config()
{
  HKEY hKey = get_reg_key();
  DWORD size = sizeof(DWORD);
  RegSetValueExA(hKey, "preview", 0, REG_DWORD, (const BYTE *)&preview, size);
  RegSetValueExA(hKey, "soundon", 0, REG_DWORD, (const BYTE *)&soundon, size);
  RegSetValueExA(hKey, "blockset", 0, REG_DWORD, (const BYTE *)&blockset, size);
  RegSetValueExA(hKey, "bgcolor1", 0, REG_DWORD, (const BYTE *)&bgcolor1, size);
}

void Display_Scale_BMP(HDC hdc, HBITMAP bmp,
                       int x, int y, int width, int height)
{

  HDC hdcMem = CreateCompatibleDC(NULL);
  HBITMAP hbmT = SelectBitmap(hdcMem, bmp);

  StretchBlt(hdc,
             x,
             cliRect.bottom - y - yPixPerPiece,
             width,
             height,
             hdcMem,
             0,
             0,
             bmSize[blockset].cx,
             bmSize[blockset].cy,
             SRCCOPY);
  SelectBitmap(hdcMem, hbmT);
  DeleteDC(hdcMem);
}

void DrawPiece(HDC hdc,
               piece p, int xp, int yp,
               int piecewidth, int pieceheight,
               HBITMAP bmp)
{
  int x, y;

  HDC hdcMem = CreateCompatibleDC(NULL);
  HBITMAP hbmT = SelectBitmap(hdcMem, bmp);
  for (x = 0; x < 4; x++)
    for (y = 0; y < 4; y++)
      if (p[y][x])
        StretchBlt(hdc,
                   xp + x * piecewidth,
                   cliRect.bottom - (yp + (y + 1) * pieceheight),
                   piecewidth,
                   pieceheight,
                   hdcMem,
                   0,
                   0,
                   bmSize[blockset].cx,
                   bmSize[blockset].cy,
                   SRCCOPY);

  /*
    BitBlt(hdc,
  xp+x*bmSize[blockset].cx, cliRect.bottom-(yp+(y+1)*bmSize[blockset].cy),
         bmSize[blockset].cx, bmSize[blockset].cy,
         hdcMem,0,0,SRCCOPY);
  */
  SelectBitmap(hdcMem, hbmT);
  DeleteDC(hdcMem);
}

void ErasePiece(HDC hdc, piece p, int xp, int yp,
                int piecewidth, int pieceheight)
{
  int x, y;

  SelectObject(hdc, GetStockObject(NULL_PEN));
  SelectObject(hdc, bgBrush);
  for (x = 0; x < 4; x++)
    for (y = 0; y < 4; y++)
      if (p[y][x])
        Rectangle(hdc, xp + x * piecewidth - 1, cliRect.bottom - (yp + y * pieceheight) + 1,
                  xp + x * piecewidth + piecewidth + 1, cliRect.bottom - (yp + y * pieceheight + pieceheight) - 1);
}

void cleartetris(tetris *t, int w, int h)
{
  int x, y;
  t->width = w;
  t->height = h;
  for (y = 0; y < t->height; y++)
    for (x = 0; x < t->width; x++)
      t->state[y][x] = FREE;
}

int loadhiscoretable(char *fname, hiscoretable hst)
{
  FILE *hstf;
  int ent;
  if ((hstf = fopen(fname, "rb")) == NULL)
  {
    perror("error loading hiscore table");
    return -1;
  }
  for (ent = 0; ent < 10; ent++)
  {
    fread(hst[ent].name, 20, 1, hstf);
    hst[ent].score = getDWORD(hstf);
    hst[ent].lines = getDWORD(hstf);
    hst[ent].level = getDWORD(hstf);
  }
  fclose(hstf);
  return 0;
}

int createhiscoretable(hiscoretable hst)
{
  int ent;
  for (ent = 0; ent < 10; ent++)
  {
    strcpy(hst[ent].name, "Nobody             ");
    hst[ent].score = (10 - ent) * 100;
    hst[ent].lines = (10 - ent);
    hst[ent].level = 1;
  }
  return 0;
}

int savehiscoretable(char *fname, hiscoretable hst)
{
  FILE *hstf;
  int ent;
  if ((hstf = fopen(fname, "wb")) == NULL)
  {
    perror("error saving hiscore table");
    return -1;
  }
  for (ent = 0; ent < 10; ent++)
  {
    fwrite(hst[ent].name, 20, 1, hstf);
    putDWORD(hst[ent].score, hstf);
    putDWORD(hst[ent].lines, hstf);
    putDWORD(hst[ent].level, hstf);
  }
  fclose(hstf);
  return 0;
}

void drawtetris(HDC hdc, tetris *t, HBITMAP bitmaps[PIECETYPES])
{
  int x, y;
  for (y = 0; y < t->height; y++)
    for (x = 0; x < t->width; x++)
      if (t->state[y][x] != FREE)
      {
        if (t->state[y][x] == CURRENT)
        {
          Display_Scale_BMP(hdc, bitmaps[currpiece],
                            x * xPixPerPiece,
                            y * yPixPerPiece,
                            xPixPerPiece,
                            yPixPerPiece);
        }
        else
          Display_Scale_BMP(hdc, bitmaps[t->state[y][x] - FIRSTPIECE],
                            x * xPixPerPiece,
                            y * yPixPerPiece,
                            xPixPerPiece,
                            yPixPerPiece);
      }
}

#define MOVE_OK 1
#define MOVE_STUCK 2
#define MOVE_PIECEDOWN 3
/* indikerar om flyttningen av biten var ok.
   MOVE_STUCK = biten kan inte flyttas f�r den
   blockeras av en annan bit eller hamnar utanf�r planen */
int movesideways(piece p, tetris *t, int x, int y)
{
  int xpos, ypos;
  for (ypos = y; ypos < y + 4; ypos++)
    for (xpos = x; xpos < x + 4; xpos++)
    {
      if (t->state[ypos][xpos] != FREE && p[ypos - y][xpos - x])
        return MOVE_STUCK;
      if (p[ypos - y][xpos - x] && (xpos >= t->width || xpos < 0))
        return MOVE_STUCK;
    }
  return MOVE_OK;
}

int movedown(piece p, tetris *t, int x, int y)
{
  int xpos, ypos;
  for (ypos = y; ypos < y + 4; ypos++)
    for (xpos = x; xpos < x + 4; xpos++)
      if ((t->state[ypos][xpos] >= FIRSTPIECE || ypos == -1) && p[ypos - y][xpos - x])
      {
        return MOVE_PIECEDOWN;
      }
  return MOVE_OK;
}

void updatetetris(int x, int y, tetris *t, piece p, int state)
{
  int xpos, ypos;

  for (ypos = y; ypos < y + 4; ypos++)
    for (xpos = x; xpos < x + 4; xpos++)
      if (p[ypos - y][xpos - x])
      {
        (t->state)[ypos][xpos] = state;
      }
}

int rotateok(piece p, tetris *t, int x, int y)
{
  int xpos, ypos;
  for (ypos = y; ypos < y + 4; ypos++)
    for (xpos = x; xpos < x + 4; xpos++)
      if (p[ypos - y][xpos - x] && (t->state[ypos][xpos] >= FIRSTPIECE || (xpos < 0 || xpos >= t->width)))
        return 0;
  return 1;
}

int trygodown()
{
  if (movedown(myPiecetable[currpiece][i], &myTetris, x, y - 1) == MOVE_OK)
  {
    updatetetris(x, y, &myTetris, myPiecetable[currpiece][i], FREE);
    y--;
    return 1;
  }
  else
    return 0;
}

void update_checkmarks()
{
  CheckMenuRadioItem(GetSubMenu(GetMenu(hMainWin), 1),
                     IDC_NOVICE,
                     IDC_EXPERT,
                     level,
                     MF_BYCOMMAND);
  CheckMenuRadioItem(GetSubMenu(GetMenu(hMainWin), 2),
                     IDC_SET1,
                     IDC_SET3,
                     blockset + IDC_SET_BASE,
                     MF_BYCOMMAND);

  CheckMenuItem(GetSubMenu(GetMenu(hMainWin), 0), IDC_SOUND, soundon ? MF_CHECKED : MF_UNCHECKED);
  CheckMenuItem(GetSubMenu(GetMenu(hMainWin), 0), IDC_PREVIEW, preview ? MF_CHECKED : MF_UNCHECKED);
  CheckMenuItem(GetSubMenu(GetMenu(hMainWin), 0), IDC_PAUSE, paused ? MF_CHECKED : MF_UNCHECKED);
}

void reinit()
{
  KillTimer(hMainWin, IDT_GAME);
  playing = 0;

  cleartetris(&myTetris, initWidth, initHeight);
  x = myTetris.width / 2 - 2;
  y = myTetris.height - 2;
  score = 0, lines = 0;
  if (level >= IDC_EXPERT)
    level = IDC_EXPERT;
  else if (level >= IDC_AMATEUR)
    level = IDC_AMATEUR;
  else
    level = IDC_NOVICE;

  nextpiece = rand() % PIECETYPES;
  currpiece = rand() % PIECETYPES;

  update_checkmarks();

  if (midi)
  {
    mciSendString("stop gametune", mciBuf, 0, 0);
    mciSendString("seek gametune to start", mciBuf, 0, 0);
  }
}

void resume()
{
  SetTimer(hMainWin, IDT_GAME, timelevel * (10 - level) + 10, NULL);
  playing = 1;
  paused = 0;
  if (midi && soundon)
    mciSendString("play gametune", mciBuf, 0, 0);
}

void pause()
{
  playing = 0;
  paused = 1;
  KillTimer(hMainWin, IDT_GAME);
}

void myKBfunc(int key)
{

  int yold = y, xold = x, iold = i;

  /* In order to calculate if a move is possible, we need to free the
     current piece first. If the move is not possible,
     we change it back again */
  updatetetris(xold, yold, &myTetris, myPiecetable[currpiece][iold], FREE);

  /* The score decreases every time the player presses a key */
  if (piecescore > 3)
    piecescore -= 3;
  switch (key)
  {

    /* Hidden debug feature */
  case 13:
    DebugWrite(&myTetris, 42);
    break;

  case KEY_LEFT:
    if (movesideways(myPiecetable[currpiece][i], &myTetris, x - 1, y) == MOVE_OK)
      x--;
    break;
  case KEY_RIGHT:
    if (movesideways(myPiecetable[currpiece][i], &myTetris, x + 1, y) == MOVE_OK)
      x++;
    break;
  case KEY_UP:
    if (rotateok(myPiecetable[currpiece][(i + 1) % 4], &myTetris, x, y))
    {
      i = (i + 1) % 4;
      if (soundon)
        PlaySound(MAKEINTRESOURCE(IDWV_ROTATE), (HINSTANCE)GetWindowLong(hMainWin, GWL_HINSTANCE),
                  SND_RESOURCE | SND_ASYNC);
    }
    break;
  case KEY_DOWN:
    while (trygodown())
      ;
    y++;
    PostMessage(hMainWin, WM_TIMER, IDT_GAME, 0);
    break;
  }

  /* We only redraw the piece if it's been moved */
  if (xold != x || yold != y || iold != i)
  {
    HDC hdc = GetDC(hPaintWin);
    updatetetris(xold, yold, &myTetris, myPiecetable[currpiece][iold], FREE);
    ErasePiece(hdc,
               myPiecetable[currpiece][iold],
               xPixPerPiece * xold, yPixPerPiece * yold,
               xPixPerPiece, yPixPerPiece);
    updatetetris(x, y, &myTetris, myPiecetable[currpiece][i], CURRENT);
    DrawPiece(hdc,
              myPiecetable[currpiece][i],
              xPixPerPiece * x,
              yPixPerPiece * y,
              xPixPerPiece,
              yPixPerPiece,
              bitmaps[blockset][currpiece]);
    ReleaseDC(hPaintWin, hdc);
  }
  else /* Change it back to the way it was and surpress drawing */
    updatetetris(xold, yold, &myTetris, myPiecetable[currpiece][iold], CURRENT);
}

/* Dialog box for the "About" box. Just has one button, "ok" */
BOOL CALLBACK
about_dlg_proc(HWND hwndDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
  switch (message)
  {

  case WM_INITDIALOG:
    SetFocus(hwndDlg);
    break;

  case WM_COMMAND:
    switch (LOWORD(wParam))
    {
    case IDOK:
    case IDCANCEL:
      EndDialog(hwndDlg, wParam);
      return TRUE;
    }
  }
  return FALSE;
}

/* This one copies the text in the edit control when user presses "ok".
   Upon initialization, it sets the edit control text to the string
   that was entered into it most recently. Additionally, it highlights
   all of the text in the control and gives focus to it. */
BOOL CALLBACK
hiscore_dlg_proc(HWND hwndDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
  int len;
  switch (message)
  {

  case WM_INITDIALOG:
    SetDlgItemText(hwndDlg, IDC_HISCORE_INPUT, namebuffer);
    SendDlgItemMessage(hwndDlg, IDC_HISCORE_INPUT, EM_SETSEL, 0, -1);
    SetFocus(GetDlgItem(hwndDlg, IDC_HISCORE_INPUT));
    break;

  case WM_COMMAND:
    switch (LOWORD(wParam))
    {
    case IDOK:
    case IDCANCEL:
      len = GetDlgItemText(hwndDlg, IDC_HISCORE_INPUT, namebuffer, 19);
      while (len < 19)
        namebuffer[len++] = ' ';
      namebuffer[19] = 0;
      EndDialog(hwndDlg, wParam);
      return TRUE;
    }
  }
  return FALSE;
}

/* This is the callback for the window that displays the hiscores
   it uses SetDlgItemText to copy the names to the window. */
BOOL CALLBACK
hiscore_popup_proc(HWND hwndDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
  int i;
  switch (message)
  {

  case WM_INITDIALOG:
    SetFocus(hwndDlg);
    for (i = 0; i < 10; i++)
    {
      char buf[150];
      sprintf(buf, "%2d.  %s%8ld%8ld%8ld", i + 1,
              hstable[i].name,
              hstable[i].score,
              hstable[i].lines,
              hstable[i].level);
      SetDlgItemText(hwndDlg, IDC_HS_BASE + i, buf);
    }
    break;

  case WM_COMMAND:
    switch (LOWORD(wParam))
    {
    case IDOK:
    case IDCANCEL:
      EndDialog(hwndDlg, wParam);
      return TRUE;
    }
  }
  return FALSE;
}

/* This function handles menu commands */
void myCommand(int command)
{

  if (command == IDC_ABOUT)
  {
    DialogBox((HINSTANCE)GetWindowLong(hMainWin, GWL_HINSTANCE),
              "ABOUTDLG", hMainWin, (DLGPROC)about_dlg_proc);
    return;
  }

  if (command == IDC_HISCORES)
  {
    DialogBox((HINSTANCE)GetWindowLong(hMainWin, GWL_HINSTANCE),
              "HISCOREPOPUP", hMainWin, (DLGPROC)hiscore_popup_proc);
    return;
  }

  /* toggle sound */
  if (command == IDC_SOUND)
  {
    if (soundon && midi)
      mciSendString("pause gametune", mciBuf, 0, 0);
    else if (midi)
      mciSendString("play gametune", mciBuf, 0, 0);
    soundon = !soundon;
  }

  /* toggle piece preview */
  if (command == IDC_PREVIEW)
  {
    preview = !preview;
  }

  if (command >= IDC_NOVICE &&
      command <= IDC_EXPERT)
  {
    level = command;
    KillTimer(hMainWin, IDT_GAME);
    SetTimer(hMainWin, IDT_GAME, timelevel * (10 - level) + 10, NULL);
  }

  switch (command)
  {
  case IDC_RESTART:
    reinit();
    resume();
    break;

  case IDC_PAUSE:
    if (paused)
      resume();
    else
      pause();
    break;

  case IDC_BGCOLOR:
    if (playing)
      pause();
    ChooseBgColor();
    if (paused)
      resume();
    break;

  case IDC_QUIT:
    PostQuitMessage(0);
    break;

  case IDC_SET1:
    blockset = 0;
    break;
  case IDC_SET2:
    blockset = 1;
    break;
  case IDC_SET3:
    blockset = 2;
    break;
  }
  update_checkmarks();
}

int isfulline(int *line, int width)
{
  int i;
  for (i = 0; i < width; i++)
    if (line[i] == FREE)
      return 0;
  return 1;
}

/* Loads the bitmap resources */
void readbmps()
{
  int i;
  BITMAP bm;
  for (i = 0; i < PIECETYPES; i++)
  {
    bitmaps[0][i] = LoadBitmap((HINSTANCE)
                                   GetWindowLong(hMainWin, GWL_HINSTANCE),
                               MAKEINTRESOURCE(IDBM_1_BASE + i));
    bitmaps[1][i] = LoadBitmap((HINSTANCE)
                                   GetWindowLong(hMainWin, GWL_HINSTANCE),
                               MAKEINTRESOURCE(IDBM_2_BASE + i));
    bitmaps[2][i] = LoadBitmap((HINSTANCE)
                                   GetWindowLong(hMainWin, GWL_HINSTANCE),
                               MAKEINTRESOURCE(IDBM_3_BASE + i));

    previews[i] = LoadBitmap((HINSTANCE)
                                 GetWindowLong(hMainWin, GWL_HINSTANCE),
                             MAKEINTRESOURCE(IDBM_PRE_BASE + i));
    if (previews[i] == NULL ||
        bitmaps[0][i] == NULL || bitmaps[1][i] == NULL || bitmaps[2][i] == NULL)
      fatal_error_win("LoadBitmap");
  }
  hBMLogo = LoadBitmap((HINSTANCE)
                           GetWindowLong(hMainWin, GWL_HINSTANCE),
                       MAKEINTRESOURCE(IDBM_LOGO));
  if (hBMLogo == NULL)
    fatal_error_win("LoadBitmap background");

  GetObject(bitmaps[0][0], sizeof(bm), &bm);
  bmSize[0].cx = bm.bmWidth;
  bmSize[0].cy = bm.bmHeight;
  GetObject(bitmaps[1][0], sizeof(bm), &bm);
  bmSize[1].cx = bm.bmWidth;
  bmSize[1].cy = bm.bmHeight;
  GetObject(bitmaps[2][0], sizeof(bm), &bm);
  bmSize[2].cx = bm.bmWidth;
  bmSize[2].cy = bm.bmHeight;
}

int lines_to_delete[5];
/* This is kind of a hack, a timer callback that erases lines
   column by column. When all columns are erased, the timer is killed
   and game is resumed.
   The lines that are to be deleted are kept in a global vector,
   "lines_to_delete" whose end is indicated by -1
    */
VOID CALLBACK LineEraser2(
    HWND hwnd,
    UINT uMsg,
    UINT idEvent,
    DWORD dwTime)
{

  HDC dc;
  static int counter = 7;

  int line = 0, p, l;

  if (counter == 0)
  {
    counter = 7;
    lines_to_delete[0] = -1;
    KillTimer(hwnd, idEvent);
    playing = 1;
    SendMessage(hMainWin, WM_PAINT, IDT_GAME, 0);
    SetTimer(hMainWin, IDT_GAME, timelevel * (10 - level) + 10, NULL);
    return;
  }
  dc = GetDC(hwnd);
  SelectObject(dc, bgBrush);
  SelectObject(dc, GetStockObject(NULL_PEN));
  /* Erase another column of all lines that are to be deleted */
  while (lines_to_delete[line] != -1)
  {
    int y = lines_to_delete[line];
    int j;
    if (counter == 1)
    {
      for (p = 0; p < myTetris.width; p++) /* clear the full line */
        myTetris.state[y][p] = FREE;

      /* move all upper lines one step down */
      for (l = y + 1; l <= myTetris.height; l++)
        for (p = 0; p < myTetris.width; p++)
          myTetris.state[l - 1][p] = myTetris.state[l][p];

      /* adjust line numbers of the rest of the lines */
      j = line + 1;
      while (lines_to_delete[j] != -1)
        lines_to_delete[j++]--;
    }

    BitBlt(dc,
           0,
           cliRect.bottom - (lines_to_delete[line] + 1) * yPixPerPiece,
           myTetris.width * xPixPerPiece,
           yPixPerPiece,
           dc,
           0,
           cliRect.bottom - (lines_to_delete[line] + 1) * yPixPerPiece,
           DSTINVERT);
    line++;
  }
  counter--;
  ReleaseDC(hwnd, dc);
}

VOID CALLBACK LineEraser1(
    HWND hwnd,
    UINT uMsg,
    UINT idEvent,
    DWORD dwTime)
{

  static int delcol = 0;
  HDC dc;

  int line = 0, p, l;

  if (delcol >= myTetris.width)
  {
    delcol = 0;
    lines_to_delete[0] = -1;
    KillTimer(hwnd, idEvent);
    playing = 1;
    SendMessage(hMainWin, WM_PAINT, IDT_GAME, 0);
    SetTimer(hMainWin, IDT_GAME, timelevel * (10 - level) + 10, NULL);
    return;
  }
  dc = GetDC(hwnd);
  SelectObject(dc, bgBrush);
  SelectObject(dc, GetStockObject(NULL_PEN));
  /* Erase another column of all lines that are to be deleted */
  while (lines_to_delete[line] != -1)
  {
    int y = lines_to_delete[line];
    int j;
    if (delcol == myTetris.width - 2)
    {
      for (p = 0; p < myTetris.width; p++) /* clear the full line */
        myTetris.state[y][p] = FREE;

      /* move all upper lines one step down */
      for (l = y + 1; l <= myTetris.height; l++)
        for (p = 0; p < myTetris.width; p++)
          myTetris.state[l - 1][p] = myTetris.state[l][p];

      /* adjust line numbers of the rest of the lines */
      j = line + 1;
      while (lines_to_delete[j] != -1)
        lines_to_delete[j++]--;
    }

    Rectangle(dc,
              0,
              cliRect.bottom - y * yPixPerPiece + 1,
              xPixPerPiece * (delcol + 1) + 1,
              cliRect.bottom - y * yPixPerPiece - yPixPerPiece - 1);
    line++;
  }
  delcol++;
}

/* This function starts the "line eraser timer" */
void deletelines(tetris *t, int nLines)
{
  switch (nLines)
  {
  case 1:
    if (soundon)
      PlaySound(MAKEINTRESOURCE(IDWV_LINE_1),
                (HINSTANCE)GetWindowLong(hMainWin, GWL_HINSTANCE),
                SND_RESOURCE | SND_ASYNC);

    LineEraser1(hPaintWin, 0, 0, 0);
    SetTimer(hPaintWin, 0, 30, LineEraser1);
    break;
  default:
    if (soundon)
      PlaySound(MAKEINTRESOURCE(IDWV_LINE_2),
                (HINSTANCE)GetWindowLong(hMainWin, GWL_HINSTANCE),
                SND_RESOURCE | SND_ASYNC);
    LineEraser2(hPaintWin, 0, 0, 0);
    SetTimer(hPaintWin, 0, 100, LineEraser2);
  }
}

void updatehiscoretable(int score, hiscoretable hst)
{
  int i, j;

  if (score > hstable[9].score)
  {
    for (i = 9; score >= hstable[i].score && i >= 0; i--)
      ;
    i++;
    if (i < 10)
    {
      DialogBox((HINSTANCE)GetWindowLong(hMainWin, GWL_HINSTANCE),
                "HISCOREDLG", hMainWin, (DLGPROC)hiscore_dlg_proc);

      /* move all scores that are lower one step down on list */
      for (j = 8; j >= i - 1; j--)
      {
        memcpy(hstable[j + 1].name, hstable[j].name, 20);
        hstable[j + 1].score = hstable[j].score;
        hstable[j + 1].lines = hstable[j].lines;
        hstable[j + 1].level = hstable[j].level;
      }

      hstable[i].score = score;
      hstable[i].lines = lines;
      hstable[i].level = level;
      memcpy(hstable[i].name, namebuffer, 20);
    }
  }

  if (midi)
  {
    mciSendString("stop gameovertune", mciBuf, 0, 0);
    mciSendString("seek gameovertune to start", mciBuf, 0, 0);
    mciSendString("play gameovertune", mciBuf, 0, 0);
  }
  DialogBox((HINSTANCE)GetWindowLong(hMainWin, GWL_HINSTANCE),
            "HISCOREPOPUP", hMainWin, (DLGPROC)hiscore_popup_proc);
  if (midi)
  {
    mciSendString("stop gameovertune", mciBuf, 0, 0);
    mciSendString("seek gameovertune to start", mciBuf, 0, 0);
  }
}

/* This is the function called when the main window receives
   a WM_TIMER message. That indicates that the block should fall
   one step down. The function checks if the block is all the way
   down and takes proper action if that is the case.
   Otherwise it just updates the board and draws the block in its
   new position.
*/
void myTimer()
{
  int xpos, ypos, xo = x, yo = y, linesatatime = 0;
  static BOOL sndPlayed = FALSE;

  if (playing)
  {
    if (movedown(myPiecetable[currpiece][i], &myTetris, x, y - 2) == MOVE_PIECEDOWN && !sndPlayed)
    {
      if (soundon)
        PlaySound(MAKEINTRESOURCE(IDWV_INPLACE),
                  (HINSTANCE)GetWindowLong(hMainWin, GWL_HINSTANCE),
                  SND_RESOURCE | SND_ASYNC);
      sndPlayed = TRUE;
    }

    if (!trygodown())
    {
      KillTimer(hMainWin, IDT_GAME);
      playing = 0;
      sndPlayed = FALSE;
      if (y >= myTetris.height - 2)
      {
        if (midi)
          mciSendString("stop gametune", mciBuf, 0, 0);
        MessageBoxW(hMainWin, "Game Over", "GDI Tetris", MB_OK);

        updatehiscoretable(score, hstable);
        return;
      }

      for (ypos = y < 0 ? 0 : y; ypos < y + 4; ypos++)
        for (xpos = x; xpos < x + 4; xpos++)
          if (myPiecetable[currpiece][i][ypos - y][xpos - x])
            myTetris.state[ypos][xpos] = FIRSTPIECE + currpiece;
      for (ypos = y < 0 ? 0 : y; ypos < y + 4; ypos++)
        if (isfulline(myTetris.state[ypos], myTetris.width))
        {
          /* mark line for deletion */
          lines_to_delete[linesatatime++] = ypos;
          if (++lines && (lines % 10 == 0) && (!levelchanged))
          {
            level++;
            levelchanged = 1;
          }
          levelchanged = 0;
        }
      /* indicate end of lines_to_delete with a -1 */
      lines_to_delete[linesatatime] = -1;
      if (linesatatime > 0)
        /* start the actual line deletion */
        deletelines(&myTetris, linesatatime);
      else
      { /*  deletelines resumes game in case linesatatime > 0 */
        playing = 1;
        SetTimer(hMainWin, IDT_GAME, timelevel * (10 - level) + 10, NULL);
      }
      if (preview)
        piecescore >>= 1;
      score += (int)piecescore + (y >> 1) + (int)(pow((float)linesatatime, 1.47) * SCORECONST * level + level);
      /* poangberakningen ar lite tagen ur luften kanske... */
      piecescore = 23;
      y = myTetris.height - 1;
      x = myTetris.width / 2 - 2;
      currpiece = nextpiece;
      nextpiece = rand() % PIECETYPES;
      i = 0;

      /* deletelines updates score window in case linesatatime > 0 */
      if (linesatatime == 0)
        SendMessage(hMainWin, WM_PAINT, IDT_SCORE, 0);
    }
    else
    {
      HDC hdc = GetDC(hPaintWin);
      updatetetris(xo, yo, &myTetris, myPiecetable[currpiece][i], FREE);
      ErasePiece(hdc,
                 myPiecetable[currpiece][i],
                 xPixPerPiece * xo, yPixPerPiece * yo,
                 xPixPerPiece, yPixPerPiece);
      updatetetris(x, y, &myTetris, myPiecetable[currpiece][i], CURRENT);

      DrawPiece(hdc,
                myPiecetable[currpiece][i],
                xPixPerPiece * x, yPixPerPiece * y,
                xPixPerPiece, yPixPerPiece,
                bitmaps[blockset][currpiece]);
      ReleaseDC(hPaintWin, hdc);
    }
  }
}

LRESULT APIENTRY
window_callback(HWND hwnd, UINT nMsg,
                WPARAM wParam, LPARAM lParam)
{
  int i;
  static HWND hScoreWin = NULL;
  DWORD len = 20;

  switch (nMsg)
  {

  case WM_CREATE:
    read_config();
    reinit();
    hMainWin = hwnd;
    GetUserName(namebuffer, &len);
    bgBrush = CreateSolidBrush(bgcolor1);
    bgBrush2 = CreateSolidBrush(bgcolor2);
    hMyFont = SetFont();
    readbmps();
    if (loadhiscoretable(hiscorefile, hstable) != 0)
      createhiscoretable(hstable);

    /* maximize window in Y-direction */
    SetWindowPos(hMainWin, HWND_TOP, 0, 0,
                 (int)((float)initWidth /
                       (float)initHeight *
                       1024.0) + 140,
                 1024.0,
                 SWP_NOMOVE);

    hPaintWin = CreateWindowEx(0,
                               "static", /* Builtin class */
                               NULL,
                               WS_VISIBLE | WS_CHILD,
                               0,
                               0,
                               initWidth * bmSize[blockset].cx,
                               initHeight * bmSize[blockset].cy,
                               hwnd, /* Parent is this window. */
                               (HMENU)1,
                               (HINSTANCE)GetWindowLong(hwnd, GWL_HINSTANCE),
                               NULL);
    if (hPaintWin == NULL)
      fatal_error_win("CreateWindowEx");

    if (!GetClientRect(hPaintWin, &cliRect))
      fatal_error_win("GetClientRect");

    hScoreWin = CreateWindowEx(0,
                               "static", /* Builtin class */
                               NULL,
                               WS_VISIBLE | WS_CHILD,
                               initWidth * bmSize[blockset].cx,
                               0,
                               140,
                               initHeight * bmSize[blockset].cy,
                               hwnd, /* Parent is this window. */
                               (HMENU)2,
                               (HINSTANCE)GetWindowLong(hwnd, GWL_HINSTANCE),
                               NULL);

    reinit();
    break;

  case WM_TIMER:
    if (playing)
      myTimer();
    break;

  case WM_COMMAND:
    myCommand(LOWORD(wParam));
    SendMessage(hMainWin, WM_PAINT, IDT_GAME, 0);
    break;

  case WM_KEYDOWN:
    myKBfunc(wParam);
    break;

  case WM_PAINT:
  {
    char str[20];
    HDC hScoreWinDC = GetDC(hScoreWin);
    HDC hScoreWinMemDC = CreateCompatibleDC(hScoreWinDC);
    HBITMAP hScreen;
    RECT hR;

    GetClientRect(hScoreWin, &hR);
    hScreen = CreateCompatibleBitmap(hScoreWinDC, hR.right, hR.bottom);
    SelectObject(hScoreWinMemDC, hScreen);

    SetBkMode(hScoreWinMemDC, TRANSPARENT);
    Display_BMP(hScoreWinMemDC, hBMLogo, 0, 0, hR.right, hR.bottom);
    if (preview && playing)
      Display_BMP(hScoreWinMemDC, previews[nextpiece], 46, 170, 50, 50);

    SelectObject(hScoreWinMemDC, hMyFont);
    sprintf(str, "Level:%4d", level);
    TextOut(hScoreWinMemDC, 25, 20, str, strlen(str));
    sprintf(str, "Score:%4ld", score);
    TextOut(hScoreWinMemDC, 25, 50, str, strlen(str));
    sprintf(str, "Lines:%4ld", lines);
    TextOut(hScoreWinMemDC, 25, 80, str, strlen(str));

    /* Copy the image from memory to screen */
    BitBlt(hScoreWinDC,
           0, 0,
           hR.right, hR.bottom,
           hScoreWinMemDC, 0, 0, SRCCOPY);

    ReleaseDC(hScoreWin, hScoreWinMemDC);
    ReleaseDC(hScoreWin, hScoreWinDC);
    DeleteObject(hScreen);

    GetUpdateRect(hScoreWin, &hR, FALSE);
    ValidateRect(hScoreWin, &hR);

    /* Update the tetris window */
    if (wParam != IDT_SCORE)
    {

      /* Draw to a memory image of the client area of paintwin.
               When all the drawing is done, copy the memory image
               to screen. This method makes the picture much more flicker free */

      HDC hPaintWinDC = GetDC(hPaintWin);
      HDC hPaintWinMemDC = CreateCompatibleDC(hPaintWinDC);
      HBITMAP hScreen;

      GetClientRect(hPaintWin, &hR);
      hScreen = CreateCompatibleBitmap(hPaintWinDC, hR.right, hR.bottom);
      SelectObject(hPaintWinMemDC, hScreen);
      FillRect(hPaintWinMemDC, &hR, bgBrush);
      drawtetris(hPaintWinMemDC, &myTetris, bitmaps[blockset]);

      /* Copy the image from memory to screen */
      BitBlt(hPaintWinDC,
             0, 0,
             hR.right, hR.bottom,
             hPaintWinMemDC, 0, 0, SRCCOPY);

      ReleaseDC(hPaintWin, hPaintWinMemDC);
      ReleaseDC(hPaintWin, hPaintWinDC);
      DeleteObject(hScreen);
      GetUpdateRect(hPaintWin, &hR, TRUE);
      ValidateRect(hPaintWin, &hR);
    }
  }
  break;

  case WM_SIZE:
  {
    int xsize = LOWORD(lParam);
    int ysize = HIWORD(lParam);
    while ((xsize - 140) % myTetris.width != 0)
      xsize--;
    while ((ysize - 0) % myTetris.height != 0)
      ysize--;

    SetWindowPos(hPaintWin, HWND_TOP, 0, 0,
                 xsize - 140,
                 ysize,
                 SWP_NOCOPYBITS);

    GetClientRect(hPaintWin, &cliRect);
    xPixPerPiece = cliRect.right / myTetris.width;
    yPixPerPiece = cliRect.bottom / myTetris.height;

    SetWindowPos(hScoreWin, HWND_TOP,
                 xPixPerPiece * myTetris.width,
                 0,
                 140, ysize,
                 SWP_NOCOPYBITS);
  }
  break;

  case WM_DESTROY:
  case WM_QUIT:
    save_config();
    DeleteObject(bgBrush);
    DeleteObject(bgBrush2);
    DeleteObject(hMyFont);
    KillTimer(hMainWin, IDT_GAME);
    for (i = 0; i < PIECETYPES; i++)
    {
      DeleteObject(bitmaps[i]);
      DeleteObject(previews[i]);
    }
    if (midi)
      mciSendString("close gametune", mciBuf, 0, 0);
    if (savehiscoretable(hiscorefile, hstable) != 0)
      fatal_error_win("Error Saving Hiscore Table");
    PostQuitMessage(0);
    return 0;
    break;
  }
  return DefWindowProc(hwnd, nMsg, wParam, lParam);
}

int APIENTRY
WinMain(
    HINSTANCE hCurrentInst,
    HINSTANCE hPreviousInst,
    LPSTR lpszCmdLine,
    int nCmdShow)
{
  WNDCLASS wndClass;
  HWND hWnd;
  MSG msg;
  char className[] = "GDI_Tetris";
  HACCEL hAccTable;

  int kbDelaySave;
  DWORD kbSpeedSave;

  /* register window class */
  wndClass.style = CS_BYTEALIGNCLIENT | CS_PARENTDC;
  wndClass.lpfnWndProc = window_callback;
  wndClass.cbClsExtra = 0;
  wndClass.cbWndExtra = 0;
  wndClass.hInstance = hCurrentInst;
  wndClass.hIcon = LoadIcon(hCurrentInst, "MYICON");
  wndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
  wndClass.hbrBackground = CreateSolidBrush(bgcolor2);
  wndClass.lpszMenuName = "MYMENU";
  wndClass.lpszClassName = className;
  RegisterClass(&wndClass);
  srand((int)GetCurrentTime());
  if (mciSendString("open .\\gditetris.mid alias gametune", mciBuf, 0, 0) != 0)
    midi = FALSE;
  else
    midi = TRUE;
  if (mciSendString("open .\\gameover.mid alias gameovertune", mciBuf, 0, 0) != 0)
    midi = FALSE;

  /* create window */
  hWnd = CreateWindowEx(WS_EX_CLIENTEDGE,
                        className, "GDI Tetris 1.0 beta",
                        WS_OVERLAPPEDWINDOW,
                        0, 0, 0, 0,
                        NULL, NULL, hCurrentInst, NULL);
  /* display window */
  ShowWindow(hWnd, nCmdShow);
  UpdateWindow(hWnd);

  /* Save old keyboard repeat/delay */
  SystemParametersInfo(SPI_GETKEYBOARDDELAY, 0, &kbDelaySave, 0);
  SystemParametersInfo(SPI_GETKEYBOARDSPEED, 0, &kbSpeedSave, 0);
  /* Set fastest speed and lowest delay */
  SystemParametersInfo(SPI_SETKEYBOARDDELAY, 0, NULL, SPIF_SENDCHANGE);
  SystemParametersInfo(SPI_SETKEYBOARDSPEED, 31, NULL, SPIF_SENDCHANGE);

  hAccTable = LoadAccelerators(hCurrentInst, "MYACCELERATORS");
  while (GetMessage(&msg, NULL, 0, 0))
  {
    TranslateAccelerator(hWnd, hAccTable, &msg);
    DispatchMessage(&msg);
  }

  DestroyAcceleratorTable(hAccTable);
  /* restore old keyboard setting */
  SystemParametersInfo(SPI_SETKEYBOARDDELAY, kbDelaySave, NULL, SPIF_SENDCHANGE);
  SystemParametersInfo(SPI_SETKEYBOARDSPEED, kbSpeedSave, NULL, SPIF_SENDCHANGE);
  return msg.wParam;
}
