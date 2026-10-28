/*
 * Diary - a simple day-by-day journal for AmigaOS 3.2 (68k).
 *
 * One window: a month calendar and the list of all entries on the left,
 * the selected day's text on the right. Entries are plain text files named
 * YYYY-MM-DD in a "Diary Entries" drawer next to the program.
 *
 * Uses ReAction (window, layout, button, listbrowser, scroller and
 * texteditor classes) as shipped with AmigaOS 3.2.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <intuition/icclass.h>
#include <libraries/gadtools.h>
#include <libraries/asl.h>
#include <graphics/text.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <classes/window.h>
#include <classes/requester.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/listbrowser.h>
#include <gadgets/scroller.h>
#include <gadgets/texteditor.h>
#include <images/bevel.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/asl.h>
#include <proto/diskfont.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/listbrowser.h>
#include <proto/scroller.h>
#include <proto/texteditor.h>
#include <proto/requester.h>
#include <clib/alib_protos.h>

#include <string.h>

static const char kVersion[] __attribute__((used)) = "$VER: Diary 1.1 (28.10.2026)";

#define kEntriesDir     "PROGDIR:Diary Entries"
#define kSettingsFile   "PROGDIR:Diary Entries/Diary Settings"
#define kMaxSearch      100     /* GM_TEXTEDITOR_Search allows up to 120 */

enum {
    GID_LIST = 1, GID_PREV, GID_TODAY, GID_NEXT, GID_DATE,
    GID_EDITOR, GID_SCROLLER, GID_CALPREV, GID_CALNEXT, GID_MONTH,
    GID_DAY0            /* GID_DAY0 + 0..41: the calendar's day cells */
};

enum {
    CMD_ABOUT = 1, CMD_SAVE, CMD_QUIT,
    CMD_UNDO, CMD_REDO, CMD_CUT, CMD_COPY, CMD_PASTE, CMD_SELECTALL,
    CMD_FIND, CMD_FINDAGAIN, CMD_SHOWALL,
    CMD_PREVDAY, CMD_NEXTDAY, CMD_TODAY,
    CMD_FONT
};

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Library *AslBase, *DiskfontBase;
struct Library *WindowBase, *LayoutBase, *ButtonBase, *ListBrowserBase;
struct Library *ScrollerBase, *TextFieldBase, *RequesterBase;

/* A list of days (days since 1 Jan 1978, the Amiga epoch), newest first. */
typedef struct {
    LONG *days;
    LONG count, capacity;
} DayList;

static DayList gAllDays, gShownDays;    /* every entry / the ones listed */
static char gSearch[kMaxSearch + 1];    /* current filter, "" = none */
static LONG gDay;                       /* the open day */
static LONG gCalYear, gCalMonth;        /* the month the calendar shows */
static BOOL gHaveFolder;

static char gFontName[64];              /* entry font, "" = default */
static UWORD gFontSize;
static struct TextAttr gTextAttr;
static struct TextFont *gTextFont;

static struct Screen *gScreen;
static struct DrawInfo *gDrawInfo;
static Object *gWindowObj, *gList, *gDateLabel, *gEditor, *gScroller;
static Object *gMonthLabel, *gDayCells[42];
static struct Window *gWindow;
static struct List gListNodes;
static char gDateText[64], gMonthText[32];
static char gDayNumbers[32][3];         /* "1".."31" for the day cells */
static BOOL gRebuilt;                   /* window was recreated: stop handling its input */

static const char *kWeekdays[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};
static const char *kMonths[] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

static struct NewMenu kMenus[] = {
    { NM_TITLE, (STRPTR)"Project",          NULL, 0, 0, NULL },
    { NM_ITEM,  (STRPTR)"About...",         NULL, 0, 0, (APTR)CMD_ABOUT },
    { NM_ITEM,  NM_BARLABEL,                NULL, 0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Save",             (STRPTR)"S", 0, 0, (APTR)CMD_SAVE },
    { NM_ITEM,  NM_BARLABEL,                NULL, 0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Quit",             (STRPTR)"Q", 0, 0, (APTR)CMD_QUIT },
    { NM_TITLE, (STRPTR)"Edit",             NULL, 0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Undo",             (STRPTR)"Z", 0, 0, (APTR)CMD_UNDO },
    { NM_ITEM,  (STRPTR)"Redo",             (STRPTR)"Y", 0, 0, (APTR)CMD_REDO },
    { NM_ITEM,  NM_BARLABEL,                NULL, 0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Cut",              (STRPTR)"X", 0, 0, (APTR)CMD_CUT },
    { NM_ITEM,  (STRPTR)"Copy",             (STRPTR)"C", 0, 0, (APTR)CMD_COPY },
    { NM_ITEM,  (STRPTR)"Paste",            (STRPTR)"V", 0, 0, (APTR)CMD_PASTE },
    { NM_ITEM,  NM_BARLABEL,                NULL, 0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Select All",       (STRPTR)"A", 0, 0, (APTR)CMD_SELECTALL },
    { NM_ITEM,  NM_BARLABEL,                NULL, 0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Find...",          (STRPTR)"F", 0, 0, (APTR)CMD_FIND },
    { NM_ITEM,  (STRPTR)"Find Again",       (STRPTR)"G", 0, 0, (APTR)CMD_FINDAGAIN },
    { NM_ITEM,  (STRPTR)"Show All Entries", NULL, 0, 0, (APTR)CMD_SHOWALL },
    { NM_TITLE, (STRPTR)"Go",               NULL, 0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Previous Day",     (STRPTR)"[", 0, 0, (APTR)CMD_PREVDAY },
    { NM_ITEM,  (STRPTR)"Next Day",         (STRPTR)"]", 0, 0, (APTR)CMD_NEXTDAY },
    { NM_ITEM,  NM_BARLABEL,                NULL, 0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Today",            (STRPTR)"T", 0, 0, (APTR)CMD_TODAY },
    { NM_TITLE, (STRPTR)"Settings",         NULL, 0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Entry Font...",    NULL, 0, 0, (APTR)CMD_FONT },
    { NM_END,   NULL,                       NULL, 0, 0, NULL }
};

/* The scroller follows the editor, and the editor follows the scroller. */
static struct TagItem kEditorToScroller[] = {
    { GA_TEXTEDITOR_Prop_First,   SCROLLER_Top },
    { GA_TEXTEDITOR_Prop_Entries, SCROLLER_Total },
    { GA_TEXTEDITOR_Prop_Visible, SCROLLER_Visible },
    { TAG_DONE, 0 }
};
static struct TagItem kScrollerToEditor[] = {
    { SCROLLER_Top, GA_TEXTEDITOR_Prop_First },
    { TAG_DONE, 0 }
};

static void RefreshList(void);
static void UpdateCalendar(void);

/* ---------- Small string helpers ---------- */

static void AppendNum(char *s, LONG n, int minDigits)
{
    char digits[12];
    int count = 0;

    s += strlen(s);
    do {
        digits[count++] = '0' + n % 10;
        n /= 10;
    } while (n > 0);
    while (count < minDigits)
        digits[count++] = '0';
    while (count > 0)
        *s++ = digits[--count];
    *s = 0;
}

/* ---------- Date arithmetic ---------- */

/* Converts days since 1 Jan 1978 to a civil date (H. Hinnant's algorithm). */
static void DayToDate(LONG day, LONG *year, LONG *month, LONG *dayOfMonth)
{
    LONG z = day + 2922 + 719468;   /* days since 1 Mar 0000 */
    LONG era = z / 146097;
    LONG doe = z - era * 146097;
    LONG yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    LONG doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    LONG mp = (5 * doy + 2) / 153;
    LONG m = mp < 10 ? mp + 3 : mp - 9;

    *year = yoe + era * 400 + (m <= 2);
    *month = m;
    *dayOfMonth = doy - (153 * mp + 2) / 5 + 1;
}

/* The inverse of DayToDate. */
static LONG DateToDay(LONG year, LONG month, LONG dayOfMonth)
{
    LONG y = year - (month <= 2);
    LONG era = y / 400;
    LONG yoe = y - era * 400;
    LONG doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + dayOfMonth - 1;
    LONG doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;

    return era * 146097 + doe - 719468 - 2922;
}

static LONG Today(void)
{
    struct DateStamp ds;

    DateStamp(&ds);
    return ds.ds_Days;
}

static int Weekday(LONG day)
{
    return (day % 7 + 7) % 7;   /* 1 Jan 1978 was a Sunday */
}

static void MakeEntryName(LONG day, char *name)
{
    LONG y, m, d;

    DayToDate(day, &y, &m, &d);
    name[0] = 0;
    AppendNum(name, y, 4);
    strcat(name, "-");
    AppendNum(name, m, 2);
    strcat(name, "-");
    AppendNum(name, d, 2);
}

/* Parses a YYYY-MM-DD file name; rejects anything else. */
static BOOL ParseEntryName(const char *name, LONG *day)
{
    static const char kPattern[] = "####-##-##";
    LONG y, m, d, checkYear, checkMonth, checkDay;
    int i;

    if (strlen(name) != 10)
        return FALSE;
    for (i = 0; i < 10; i++) {
        char c = name[i];
        if (kPattern[i] == '#' ? (c < '0' || c > '9') : c != '-')
            return FALSE;
    }
    y = (name[0] - '0') * 1000 + (name[1] - '0') * 100 + (name[2] - '0') * 10 + (name[3] - '0');
    m = (name[5] - '0') * 10 + (name[6] - '0');
    d = (name[8] - '0') * 10 + (name[9] - '0');
    if (y < 1978 || m < 1 || m > 12 || d < 1 || d > 31)
        return FALSE;

    *day = DateToDay(y, m, d);
    DayToDate(*day, &checkYear, &checkMonth, &checkDay);    /* rejects e.g. 31 April */
    return checkYear == y && checkMonth == m && checkDay == d;
}

static void MakeLongDate(LONG day, char *s)
{
    LONG y, m, d;

    DayToDate(day, &y, &m, &d);
    strcpy(s, kWeekdays[Weekday(day)]);
    strcat(s, ", ");
    AppendNum(s, d, 1);
    strcat(s, " ");
    strcat(s, kMonths[m - 1]);
    strcat(s, " ");
    AppendNum(s, y, 4);
}

static void MakeShortDate(LONG day, char *s)
{
    LONG y, m, d;

    DayToDate(day, &y, &m, &d);
    strncpy(s, kWeekdays[Weekday(day)], 3);
    s[3] = 0;
    strcat(s, " ");
    AppendNum(s, d, 2);
    strcat(s, " ");
    strncat(s, kMonths[m - 1], 3);
    strcat(s, " ");
    AppendNum(s, y, 4);
}

/* ---------- Day lists ---------- */

static LONG FindDay(const DayList *list, LONG day)
{
    LONG i;

    for (i = 0; i < list->count; i++)
        if (list->days[i] == day)
            return i;
    return -1;
}

static BOOL ReserveDays(DayList *list, LONG capacity)
{
    LONG *days;

    if (capacity <= list->capacity)
        return TRUE;
    capacity += 64;
    days = AllocVec(capacity * sizeof(LONG), MEMF_ANY);
    if (days == NULL)
        return FALSE;
    if (list->days != NULL) {
        CopyMem(list->days, days, list->count * sizeof(LONG));
        FreeVec(list->days);
    }
    list->days = days;
    list->capacity = capacity;
    return TRUE;
}

/* Inserts a day, keeping the list sorted newest first. */
static BOOL InsertDay(DayList *list, LONG day)
{
    LONG i, j;

    if (FindDay(list, day) >= 0)
        return TRUE;
    if (!ReserveDays(list, list->count + 1))
        return FALSE;
    for (i = 0; i < list->count && list->days[i] > day; i++)
        ;
    for (j = list->count; j > i; j--)
        list->days[j] = list->days[j - 1];
    list->days[i] = day;
    list->count++;
    return TRUE;
}

static void RemoveDay(DayList *list, LONG day)
{
    LONG i = FindDay(list, day);

    if (i < 0)
        return;
    for (; i < list->count - 1; i++)
        list->days[i] = list->days[i + 1];
    list->count--;
}

static BOOL CopyDays(DayList *to, const DayList *from)
{
    if (!ReserveDays(to, from->count))
        return FALSE;
    if (from->count > 0)
        CopyMem(from->days, to->days, from->count * sizeof(LONG));
    to->count = from->count;
    return TRUE;
}

static void FreeDays(DayList *list)
{
    if (list->days != NULL)
        FreeVec(list->days);
    list->days = NULL;
    list->count = list->capacity = 0;
}

/* ---------- Messages ---------- */

static LONG Ask(const char *text, const char *buttons)
{
    struct EasyStruct es;

    es.es_StructSize = sizeof(es);
    es.es_Flags = 0;
    es.es_Title = (STRPTR)"Diary";
    es.es_TextFormat = (STRPTR)text;
    es.es_GadgetFormat = (STRPTR)buttons;
    return EasyRequestArgs(gWindow, &es, NULL, NULL);
}

static void ShowMessage(const char *text)
{
    Ask(text, "OK");
}

static void ShowError(const char *text)
{
    char message[200];
    char reason[80];

    strcpy(message, text);
    if (IoErr() != 0 && Fault(IoErr(), NULL, reason, sizeof(reason))) {
        strcat(message, "\n");
        strcat(message, reason);
    }
    ShowMessage(message);
}

static void ShowAbout(void)
{
    ShowMessage("Diary 1.1\n\n"
                "A simple day-by-day journal for AmigaOS 3.2.\n"
                "Entries are kept in the \"Diary Entries\" drawer.");
}

static void SetBusy(BOOL busy)
{
    SetAttrs(gWindowObj, WA_BusyPointer, busy, TAG_DONE);
}

/* ---------- Entry files ---------- */

static void EntryPath(LONG day, char *path)
{
    strcpy(path, kEntriesDir "/");
    MakeEntryName(day, path + strlen(path));
}

static void FindEntriesFolder(void)
{
    BPTR lock = Lock(kEntriesDir, ACCESS_READ);

    if (lock == 0)
        lock = CreateDir(kEntriesDir);
    gHaveFolder = lock != 0;
    if (lock != 0)
        UnLock(lock);
}

static void ScanEntries(void)
{
    struct FileInfoBlock *fib;
    BPTR lock;

    if (!gHaveFolder)
        return;
    fib = AllocDosObject(DOS_FIB, NULL);
    lock = Lock(kEntriesDir, ACCESS_READ);
    if (fib != NULL && lock != 0 && Examine(lock, fib)) {
        while (ExNext(lock, fib)) {
            LONG day;
            if (fib->fib_DirEntryType < 0 && fib->fib_Size > 0
                    && ParseEntryName(fib->fib_FileName, &day))
                InsertDay(&gAllDays, day);
        }
    }
    if (lock != 0)
        UnLock(lock);
    if (fib != NULL)
        FreeDosObject(DOS_FIB, fib);
}

/* ISO Latin-1 for MacRoman 0x80..0xFF; plain look-alikes for typographic
   characters, '?' where Latin-1 has no match. */
static const UBYTE kMacToLatin1[128] = {
    0xC4, 0xC5, 0xC7, 0xC9, 0xD1, 0xD6, 0xDC, 0xE1,  /* 80 */
    0xE0, 0xE2, 0xE4, 0xE3, 0xE5, 0xE7, 0xE9, 0xE8,  /* 88 */
    0xEA, 0xEB, 0xED, 0xEC, 0xEE, 0xEF, 0xF1, 0xF3,  /* 90 */
    0xF2, 0xF4, 0xF6, 0xF5, 0xFA, 0xF9, 0xFB, 0xFC,  /* 98 */
    0x2B, 0xB0, 0xA2, 0xA3, 0xA7, 0xB7, 0xB6, 0xDF,  /* A0 */
    0xAE, 0xA9, 0x3F, 0xB4, 0xA8, 0x3F, 0xC6, 0xD8,  /* A8 */
    0x3F, 0xB1, 0x3F, 0x3F, 0xA5, 0xB5, 0x3F, 0x3F,  /* B0 */
    0x3F, 0x3F, 0x3F, 0xAA, 0xBA, 0x3F, 0xE6, 0xF8,  /* B8 */
    0xBF, 0xA1, 0xAC, 0x3F, 0x3F, 0x3F, 0x3F, 0xAB,  /* C0 */
    0xBB, 0x2E, 0xA0, 0xC0, 0xC3, 0xD5, 0x3F, 0x3F,  /* C8 */
    0x2D, 0x2D, 0x22, 0x22, 0x27, 0x27, 0xF7, 0x3F,  /* D0 */
    0xFF, 0x3F, 0x2F, 0x3F, 0x3C, 0x3E, 0x3F, 0x3F,  /* D8 */
    0x3F, 0xB7, 0x2C, 0x22, 0x3F, 0xC2, 0xCA, 0xC1,  /* E0 */
    0xCB, 0xC8, 0xCD, 0xCE, 0xCF, 0xCC, 0xD3, 0xD4,  /* E8 */
    0x3F, 0xD2, 0xDA, 0xDB, 0xD9, 0x3F, 0x5E, 0x7E,  /* F0 */
    0xAF, 0x3F, 0x3F, 0x3F, 0xB8, 0x3F, 0x3F, 0x3F   /* F8 */
};

/* Converts text from the Mac version in place: CR and CR LF line ends become
   LF, and MacRoman characters become Latin-1. Diary always ends its files
   with LF, so text without any LF, or with bytes 0x80..0x9F (which only
   MacRoman uses), came from the Mac. Returns the new length. */
static LONG ConvertForeignText(UBYTE *text, LONG size)
{
    BOOL hasLF = FALSE, hasC1 = FALSE, isMacRoman;
    LONG i, j;

    for (i = 0; i < size; i++) {
        if (text[i] == '\n')
            hasLF = TRUE;
        else if (text[i] >= 0x80 && text[i] < 0xA0)
            hasC1 = TRUE;
    }
    isMacRoman = !hasLF || hasC1;

    for (i = j = 0; i < size; i++) {
        UBYTE c = text[i];

        if (c == '\r') {
            c = '\n';
            if (i + 1 < size && text[i + 1] == '\n')
                i++;
        } else if (c >= 0x80 && isMacRoman) {
            c = kMacToLatin1[c - 0x80];
        }
        text[j++] = c;
    }
    return j;
}

/* Reads a whole file into a new NUL-terminated buffer (free with FreeVec),
   converting text from the Mac version. Returns NULL if the file doesn't
   exist or can't be read. */
static char *ReadTextFile(const char *path, LONG *length)
{
    BPTR file = Open(path, MODE_OLDFILE);
    char *buf = NULL;
    LONG size;

    if (file == 0)
        return NULL;
    Seek(file, 0, OFFSET_END);
    size = Seek(file, 0, OFFSET_BEGINNING);
    if (size >= 0)
        buf = AllocVec(size + 1, MEMF_ANY);
    if (buf != NULL && Read(file, buf, size) != size) {
        FreeVec(buf);
        buf = NULL;
    }
    Close(file);
    if (buf == NULL)
        return NULL;

    *length = ConvertForeignText((UBYTE *)buf, size);
    buf[*length] = 0;
    return buf;
}

static BOOL WriteTextFile(const char *path, const char *text, LONG length)
{
    BPTR file = Open(path, MODE_NEWFILE);
    BOOL ok;

    if (file == 0)
        return FALSE;
    ok = Write(file, (APTR)text, length) == length && Write(file, (APTR)"\n", 1) == 1;
    if (!Close(file))
        ok = FALSE;
    return ok;
}

/* ---------- Settings ---------- */

static void LoadSettings(void)
{
    LONG length, size = 0;
    char *text = ReadTextFile(kSettingsFile, &length);
    char *p;

    if (text == NULL)
        return;
    /* "font name\nsize\n" */
    p = strchr(text, '\n');
    if (p != NULL && p - text < (LONG)sizeof(gFontName)) {
        *p++ = 0;
        while (*p >= '0' && *p <= '9')
            size = size * 10 + (*p++ - '0');
        if (text[0] != 0 && size > 0 && size < 256) {
            strcpy(gFontName, text);
            gFontSize = size;
        }
    }
    FreeVec(text);
}

static void SaveSettings(void)
{
    char text[sizeof(gFontName) + 8];

    if (!gHaveFolder)
        return;
    strcpy(text, gFontName);
    strcat(text, "\n");
    AppendNum(text, gFontSize, 1);
    WriteTextFile(kSettingsFile, text, strlen(text));
}

/* Opens the chosen entry font, falling back to the default if it's gone. */
static void OpenTextFont(void)
{
    if (gTextFont != NULL)
        CloseFont(gTextFont);
    gTextFont = NULL;
    if (gFontName[0] == 0)
        return;
    gTextAttr.ta_Name = (STRPTR)gFontName;
    gTextAttr.ta_YSize = gFontSize;
    gTextAttr.ta_Style = FS_NORMAL;
    gTextAttr.ta_Flags = 0;
    gTextFont = OpenDiskFont(&gTextAttr);
    if (gTextFont == NULL)
        gFontName[0] = 0;
}

/* ---------- The open entry ---------- */

static BOOL EditorChanged(void)
{
    ULONG changed = FALSE;

    GetAttr(GA_TEXTEDITOR_HasChanged, gEditor, &changed);
    return changed != FALSE;
}

static void EditorCommand(const char *command)
{
    DoGadgetMethod((struct Gadget *)gEditor, gWindow, NULL,
                   GM_TEXTEDITOR_ARexxCmd, NULL, (STRPTR)command);
}

static void ShowDate(void)
{
    MakeLongDate(gDay, gDateText);
    SetGadgetAttrs((struct Gadget *)gDateLabel, gWindow, NULL, GA_Text, gDateText, TAG_DONE);
}

static void ShowMonthOf(LONG day)
{
    LONG y, m, d;

    DayToDate(day, &y, &m, &d);
    gCalYear = y;
    gCalMonth = m;
    UpdateCalendar();
}

static void SyncListSelection(void)
{
    LONG index = FindDay(&gShownDays, gDay);

    SetGadgetAttrs((struct Gadget *)gList, gWindow, NULL,
                   LISTBROWSER_Selected, index,
                   index >= 0 ? LISTBROWSER_MakeVisible : TAG_IGNORE, index,
                   TAG_DONE);
}

static void LoadEntry(void)
{
    char path[64];
    LONG length;
    char *text;

    EntryPath(gDay, path);
    text = ReadTextFile(path, &length);
    if (text != NULL && length > 0 && text[length - 1] == '\n')
        text[length - 1] = 0;       /* SaveEntry adds one line end */

    SetGadgetAttrs((struct Gadget *)gEditor, gWindow, NULL,
                   GA_TEXTEDITOR_Contents, text != NULL ? text : "",
                   TAG_DONE);
    SetGadgetAttrs((struct Gadget *)gEditor, gWindow, NULL,
                   GA_TEXTEDITOR_HasChanged, FALSE, TAG_DONE);
    if (text != NULL)
        FreeVec(text);
    EditorCommand("POSITION EOF");

    ShowDate();
    ShowMonthOf(gDay);
    SyncListSelection();
}

static BOOL SaveEntry(void)
{
    char path[64];
    STRPTR text;
    LONG length;

    if (!EditorChanged())
        return TRUE;
    if (!gHaveFolder) {
        ShowMessage("There is no \"Diary Entries\" drawer to save into.");
        return FALSE;
    }

    text = (STRPTR)DoGadgetMethod((struct Gadget *)gEditor, gWindow, NULL,
                                  GM_TEXTEDITOR_ExportText, NULL);
    if (text == NULL) {
        ShowMessage("Not enough memory to save the entry.");
        return FALSE;
    }
    length = strlen(text);
    while (length > 0 && text[length - 1] == '\n')
        length--;

    EntryPath(gDay, path);
    if (length == 0) {
        /* An emptied entry is removed rather than kept as an empty file. */
        FreeVec(text);
        DeleteFile(path);
        RemoveDay(&gAllDays, gDay);
        RemoveDay(&gShownDays, gDay);
        RefreshList();
        UpdateCalendar();
    } else {
        BOOL ok = WriteTextFile(path, text, length);

        FreeVec(text);
        if (!ok) {
            ShowError("Could not save the entry.");
            return FALSE;
        }
        if (FindDay(&gAllDays, gDay) < 0) {
            InsertDay(&gAllDays, gDay);
            if (gSearch[0] == 0)
                InsertDay(&gShownDays, gDay);
            RefreshList();
            UpdateCalendar();
        }
    }
    SetGadgetAttrs((struct Gadget *)gEditor, gWindow, NULL,
                   GA_TEXTEDITOR_HasChanged, FALSE, TAG_DONE);
    return TRUE;
}

static void GoToDay(LONG day)
{
    if (day == gDay) {
        SyncListSelection();
        return;
    }
    if (!SaveEntry()) {
        SyncListSelection();
        return;
    }
    gDay = day;
    LoadEntry();
}

/* ---------- Searching ---------- */

/* Case folding for ASCII and the Latin-1 letters. */
static UBYTE FoldCase(UBYTE c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7))
        return c + 32;
    return c;
}

static BOOL TextContains(const char *text, LONG length, const char *pattern)
{
    LONG n = strlen(pattern), i, j;

    for (i = 0; i + n <= length; i++) {
        for (j = 0; j < n; j++)
            if (FoldCase(text[i + j]) != FoldCase(pattern[j]))
                break;
        if (j == n)
            return TRUE;
    }
    return FALSE;
}

static BOOL EntryContains(LONG day)
{
    char path[64];
    LONG length;
    char *text;
    BOOL found;

    EntryPath(day, path);
    text = ReadTextFile(path, &length);
    if (text == NULL)
        return FALSE;
    found = TextContains(text, length, gSearch);
    FreeVec(text);
    return found;
}

/* Selects the first match in the open entry, or the next one after the cursor. */
static BOOL SelectMatch(BOOL fromTop)
{
    return DoGadgetMethod((struct Gadget *)gEditor, gWindow, NULL,
                          GM_TEXTEDITOR_Search, NULL, (STRPTR)gSearch,
                          fromTop ? GF_TEXTEDITOR_Search_FromTop
                                  : GF_TEXTEDITOR_Search_Next) != 0;
}

static void OpenDay(LONG day)
{
    GoToDay(day);
    if (gSearch[0] != 0 && day == gDay)
        SelectMatch(TRUE);
}

static void ShowAllEntries(void)
{
    gSearch[0] = 0;
    CopyDays(&gShownDays, &gAllDays);
    RefreshList();
    UpdateCalendar();
}

static BOOL AskForSearchText(char *text)
{
    Object *req;
    LONG result = 0;

    req = RequesterObject,
        REQ_Type, REQTYPE_STRING,
        REQ_TitleText, "Find",
        REQ_BodyText, "Find entries containing:",
        REQ_GadgetText, "_Find|_Cancel",
        REQS_Buffer, text,
        REQS_MaxChars, kMaxSearch,
        REQS_AllowEmpty, TRUE,
        End;
    if (req == NULL)
        return FALSE;
    result = DoMethod(req, RM_OPENREQ, NULL, gWindow, NULL, TAG_DONE);
    DisposeObject(req);
    return result == 1;
}

static void DoFind(void)
{
    char text[kMaxSearch + 1], previous[kMaxSearch + 1];
    DayList matches = { NULL, 0, 0 };
    LONG i;

    strcpy(text, gSearch);
    if (!AskForSearchText(text))
        return;
    if (text[0] == 0) {
        ShowAllEntries();
        return;
    }
    if (!SaveEntry())
        return;

    SetBusy(TRUE);
    strcpy(previous, gSearch);
    strcpy(gSearch, text);
    for (i = 0; i < gAllDays.count; i++)
        if (EntryContains(gAllDays.days[i]))
            InsertDay(&matches, gAllDays.days[i]);
    SetBusy(FALSE);

    if (matches.count == 0) {
        ShowMessage("No entries contain that text.");
        strcpy(gSearch, previous);      /* keep the current filter */
    } else {
        CopyDays(&gShownDays, &matches);
        RefreshList();
        UpdateCalendar();
        if (FindDay(&gShownDays, gDay) < 0)
            GoToDay(gShownDays.days[0]);
        SelectMatch(TRUE);
    }
    FreeDays(&matches);
}

/* Moves to the next match: first within the open entry, then on to the
   following (older) matching entries, wrapping around to the newest. */
static void DoFindAgain(void)
{
    LONG start, step;

    if (gSearch[0] == 0) {
        DoFind();
        return;
    }
    if (SelectMatch(FALSE))
        return;

    start = FindDay(&gShownDays, gDay);
    for (step = 1; step <= gShownDays.count; step++) {
        LONG i = start < 0 ? step - 1 : (start + step) % gShownDays.count;
        LONG day = gShownDays.days[i];

        if (day == gDay) {
            if (SelectMatch(TRUE))
                return;
        } else if (EntryContains(day)) {
            GoToDay(day);
            if (day == gDay)
                SelectMatch(TRUE);
            return;
        }
    }
    DisplayBeep(gScreen);
}

/* ---------- Entry list ---------- */

static void RefreshList(void)
{
    LONG i;

    SetGadgetAttrs((struct Gadget *)gList, gWindow, NULL, LISTBROWSER_Labels, ~0, TAG_DONE);
    FreeListBrowserList(&gListNodes);
    NewList(&gListNodes);
    for (i = 0; i < gShownDays.count; i++) {
        char label[32];
        struct Node *node;

        MakeShortDate(gShownDays.days[i], label);
        node = AllocListBrowserNode(1,
                                    LBNA_UserData, gShownDays.days[i],
                                    LBNA_Column, 0,
                                    LBNCA_CopyText, TRUE,
                                    LBNCA_Text, label,
                                    TAG_DONE);
        if (node != NULL)
            AddTail(&gListNodes, node);
    }
    SetGadgetAttrs((struct Gadget *)gList, gWindow, NULL, LISTBROWSER_Labels, &gListNodes, TAG_DONE);
    SyncListSelection();
}

static void DoListClick(void)
{
    struct Node *node = NULL;
    ULONG day;

    GetAttr(LISTBROWSER_SelectedNode, gList, (ULONG *)&node);
    if (node == NULL)
        return;
    GetListBrowserNodeAttrs(node, LBNA_UserData, &day, TAG_DONE);
    OpenDay((LONG)day);
}

/* ---------- Calendar ---------- */

/* The calendar is made of buttons: a title row (arrows and month), a row of
   weekday initials and 42 day cells, numbered row by row from a Sunday.
   Days with an entry are bold, today is framed and the open day is filled. */

static LONG CalendarFirstDay(void)
{
    return DateToDay(gCalYear, gCalMonth, 1);
}

static LONG CalendarMonthLength(void)
{
    LONG next = gCalMonth == 12 ? DateToDay(gCalYear + 1, 1, 1)
                                : DateToDay(gCalYear, gCalMonth + 1, 1);
    return next - CalendarFirstDay();
}

static void UpdateCalendar(void)
{
    LONG first = CalendarFirstDay(), length = CalendarMonthLength(), today = Today();
    int lead = Weekday(first), i;
    UWORD *pens = gDrawInfo->dri_Pens;

    if (gMonthLabel == NULL)
        return;
    strcpy(gMonthText, kMonths[gCalMonth - 1]);
    strcat(gMonthText, " ");
    AppendNum(gMonthText, gCalYear, 4);
    SetGadgetAttrs((struct Gadget *)gMonthLabel, gWindow, NULL, GA_Text, gMonthText, TAG_DONE);

    for (i = 0; i < 42; i++) {
        LONG n = i - lead + 1, day = first + n - 1;
        BOOL used = n >= 1 && n <= length;
        BOOL open = used && day == gDay;

        SetGadgetAttrs((struct Gadget *)gDayCells[i], gWindow, NULL,
                       GA_ReadOnly, !used,
                       BUTTON_BevelStyle, used && day == today ? BVS_THIN : BVS_NONE,
                       BUTTON_BackgroundPen, open ? pens[FILLPEN] : ~0UL,
                       BUTTON_TextPen, open ? pens[FILLTEXTPEN] : ~0UL,
                       BUTTON_SoftStyle, used && FindDay(&gShownDays, day) >= 0 ? FSF_BOLD : FS_NORMAL,
                       GA_Text, used ? gDayNumbers[n] : "",
                       TAG_DONE);
    }
}

static void ChangeCalendarMonth(int delta)
{
    gCalMonth += delta;
    if (gCalMonth < 1) {
        gCalMonth = 12;
        gCalYear--;
    } else if (gCalMonth > 12) {
        gCalMonth = 1;
        gCalYear++;
    }
    UpdateCalendar();
}

static void DoDayClick(int cell)
{
    LONG first = CalendarFirstDay();
    LONG index = cell - Weekday(first);

    if (index >= 0 && index < CalendarMonthLength())
        OpenDay(first + index);
}

/* ---------- Window ---------- */

static Object *MakeButton(ULONG id, const char *text)
{
    return ButtonObject,
        GA_ID, id,
        GA_RelVerify, TRUE,
        GA_Text, text,
        End;
}

/* A flat text label, used for the month and the weekday initials. */
static Object *MakeLabel(ULONG id, const char *text, BOOL bold)
{
    return ButtonObject,
        GA_ID, id,
        GA_ReadOnly, TRUE,
        GA_Text, text,
        BUTTON_BevelStyle, BVS_NONE,
        BUTTON_SoftStyle, bold ? FSF_BOLD : FS_NORMAL,
        End;
}

static Object *MakeDayCell(int cell)
{
    return gDayCells[cell] = ButtonObject,
        GA_ID, GID_DAY0 + cell,
        GA_RelVerify, TRUE,
        GA_Text, "",
        BUTTON_DomainString, "000",     /* room for the frame around today */
        BUTTON_BevelStyle, BVS_NONE,
        End;
}

static Object *MakeWeekRow(int row)
{
    int c = row * 7;

    return HLayoutObject,
        LAYOUT_EvenSize, TRUE,
        LAYOUT_SpaceInner, FALSE,
        LAYOUT_AddChild, MakeDayCell(c),
        LAYOUT_AddChild, MakeDayCell(c + 1),
        LAYOUT_AddChild, MakeDayCell(c + 2),
        LAYOUT_AddChild, MakeDayCell(c + 3),
        LAYOUT_AddChild, MakeDayCell(c + 4),
        LAYOUT_AddChild, MakeDayCell(c + 5),
        LAYOUT_AddChild, MakeDayCell(c + 6),
        End;
}

static Object *MakeCalendar(void)
{
    return VLayoutObject,
        LAYOUT_SpaceInner, FALSE,
        LAYOUT_AddChild, HLayoutObject,
            LAYOUT_AddChild, MakeButton(GID_CALPREV, "<"),
            CHILD_WeightedWidth, 0,
            LAYOUT_AddChild, gMonthLabel = MakeLabel(GID_MONTH, gMonthText, TRUE),
            LAYOUT_AddChild, MakeButton(GID_CALNEXT, ">"),
            CHILD_WeightedWidth, 0,
            End,
        LAYOUT_AddChild, HLayoutObject,
            LAYOUT_EvenSize, TRUE,
            LAYOUT_SpaceInner, FALSE,
            LAYOUT_AddChild, MakeLabel(0, "S", FALSE),
            LAYOUT_AddChild, MakeLabel(0, "M", FALSE),
            LAYOUT_AddChild, MakeLabel(0, "T", FALSE),
            LAYOUT_AddChild, MakeLabel(0, "W", FALSE),
            LAYOUT_AddChild, MakeLabel(0, "T", FALSE),
            LAYOUT_AddChild, MakeLabel(0, "F", FALSE),
            LAYOUT_AddChild, MakeLabel(0, "S", FALSE),
            End,
        LAYOUT_AddChild, MakeWeekRow(0),
        LAYOUT_AddChild, MakeWeekRow(1),
        LAYOUT_AddChild, MakeWeekRow(2),
        LAYOUT_AddChild, MakeWeekRow(3),
        LAYOUT_AddChild, MakeWeekRow(4),
        LAYOUT_AddChild, MakeWeekRow(5),
        End;
}

static BOOL CreateWindowObject(void)
{
    Object *layout;

    layout = LayoutObject,
        LAYOUT_DeferLayout, TRUE,
        LAYOUT_SpaceOuter, TRUE,

        LAYOUT_AddChild, VLayoutObject,
            LAYOUT_AddChild, MakeCalendar(),
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, gList = ListBrowserObject,
                GA_ID, GID_LIST,
                GA_RelVerify, TRUE,
                LISTBROWSER_Labels, &gListNodes,
                LISTBROWSER_ShowSelected, TRUE,
                End,
            End,
        CHILD_WeightedWidth, 0,

        LAYOUT_AddChild, VLayoutObject,
            LAYOUT_AddChild, HLayoutObject,
                LAYOUT_AddChild, MakeButton(GID_PREV, "<"),
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, MakeButton(GID_TODAY, "Today"),
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, MakeButton(GID_NEXT, ">"),
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, gDateLabel = ButtonObject,
                    GA_ID, GID_DATE,
                    GA_ReadOnly, TRUE,
                    GA_Text, gDateText,
                    BUTTON_BevelStyle, BVS_NONE,
                    BUTTON_Justification, BCJ_LEFT,
                    BUTTON_SoftStyle, FSF_BOLD,
                    End,
                End,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, HLayoutObject,
                LAYOUT_SpaceInner, FALSE,
                LAYOUT_AddChild, gEditor = TextEditorObject,
                    GA_ID, GID_EDITOR,
                    GA_RelVerify, TRUE,
                    gTextFont != NULL ? GA_TextAttr : TAG_IGNORE, &gTextAttr,
                    End,
                LAYOUT_AddChild, gScroller = ScrollerObject,
                    GA_ID, GID_SCROLLER,
                    SCROLLER_Orientation, SORIENT_VERT,
                    SCROLLER_Arrows, TRUE,
                    End,
                CHILD_WeightedWidth, 0,
                End,
            End,
        End;
    if (layout == NULL)
        return FALSE;

    SetAttrs(gEditor, ICA_TARGET, gScroller, ICA_MAP, kEditorToScroller, TAG_DONE);
    SetAttrs(gScroller, ICA_TARGET, gEditor, ICA_MAP, kScrollerToEditor, TAG_DONE);

    gWindowObj = WindowObject,
        WA_Title, "Diary",
        WA_ScreenTitle, "Diary 1.1",
        WA_PubScreen, gScreen,
        WA_Activate, TRUE,
        WA_DragBar, TRUE,
        WA_DepthGadget, TRUE,
        WA_CloseGadget, TRUE,
        WA_SizeGadget, TRUE,
        WA_InnerWidth, gScreen->Width < 680 ? gScreen->Width - 40 : 640,
        WA_InnerHeight, gScreen->Height < 400 ? gScreen->Height - 60 : 340,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_MENUPICK | IDCMP_NEWSIZE,
        WINDOW_Position, WPOS_CENTERSCREEN,
        WINDOW_NewMenu, kMenus,
        WINDOW_Layout, layout,
        End;
    if (gWindowObj == NULL) {
        DisposeObject(layout);
        return FALSE;
    }
    gWindow = (struct Window *)DoMethod(gWindowObj, WM_OPEN, NULL);
    return gWindow != NULL;
}

static void DisposeWindowObject(void)
{
    if (gWindowObj != NULL) {
        DoMethod(gWindowObj, WM_CLOSE, NULL);
        DisposeObject(gWindowObj);
    }
    gWindowObj = NULL;
    gWindow = NULL;
    gMonthLabel = NULL;
}

/* Fonts can't be changed in an open texteditor, so the window is rebuilt. */
static void ChooseFont(void)
{
    struct FontRequester *fr;
    struct TextFont *font = gWindow->RPort->Font;

    fr = AllocAslRequestTags(ASL_FontRequest,
                             ASLFO_TitleText, "Entry Font",
                             ASLFO_InitialName, gFontName[0] ? gFontName : (char *)font->tf_Message.mn_Node.ln_Name,
                             ASLFO_InitialSize, gFontName[0] ? gFontSize : font->tf_YSize,
                             ASLFO_MaxHeight, 48,
                             TAG_DONE);
    if (fr == NULL)
        return;
    if (AslRequestTags(fr, ASLFO_Window, gWindow, TAG_DONE)
            && strlen((char *)fr->fo_Attr.ta_Name) < sizeof(gFontName)) {
        if (SaveEntry()) {
            strcpy(gFontName, (char *)fr->fo_Attr.ta_Name);
            gFontSize = fr->fo_Attr.ta_YSize;
            DisposeWindowObject();
            OpenTextFont();
            if (!CreateWindowObject()) {
                gFontName[0] = 0;       /* try again with the default font */
                OpenTextFont();
                DisposeWindowObject();
                CreateWindowObject();
            }
            if (gWindow != NULL) {
                RefreshList();
                LoadEntry();
            }
            SaveSettings();
            gRebuilt = TRUE;
        }
    }
    FreeAslRequest(fr);
}

/* ---------- Commands ---------- */

static BOOL DoQuit(void)
{
    if (SaveEntry())
        return TRUE;
    return Ask("The entry could not be saved.\nQuit anyway?", "Quit|Cancel") == 1;
}

/* Returns TRUE when Diary should quit. */
static BOOL DoCommand(ULONG command)
{
    switch (command) {
    case CMD_ABOUT:     ShowAbout(); break;
    case CMD_SAVE:      SaveEntry(); break;
    case CMD_QUIT:      return DoQuit();
    case CMD_UNDO:      EditorCommand("UNDO"); break;
    case CMD_REDO:      EditorCommand("REDO"); break;
    case CMD_CUT:       EditorCommand("CUT"); break;
    case CMD_COPY:      EditorCommand("COPY"); break;
    case CMD_PASTE:     EditorCommand("PASTE"); break;
    case CMD_SELECTALL:
        EditorCommand("POSITION SOF");
        EditorCommand("MARK ON");
        EditorCommand("POSITION EOF");
        break;
    case CMD_FIND:      DoFind(); break;
    case CMD_FINDAGAIN: DoFindAgain(); break;
    case CMD_SHOWALL:   ShowAllEntries(); break;
    case CMD_PREVDAY:   OpenDay(gDay - 1); break;
    case CMD_NEXTDAY:   OpenDay(gDay + 1); break;
    case CMD_TODAY:     OpenDay(Today()); break;
    case CMD_FONT:      ChooseFont(); break;
    }
    return FALSE;
}

static BOOL DoMenuPick(UWORD code)
{
    struct Menu *strip = NULL;
    struct MenuItem *item;

    GetAttr(WINDOW_MenuStrip, gWindowObj, (ULONG *)&strip);
    if (code == MENUNULL || strip == NULL)
        return FALSE;
    item = ItemAddress(strip, code);
    if (item == NULL)
        return FALSE;
    return DoCommand((ULONG)GTMENUITEM_USERDATA(item));
}

static BOOL DoGadgetUp(UWORD id)
{
    if (id >= GID_DAY0 && id < GID_DAY0 + 42) {
        DoDayClick(id - GID_DAY0);
        return FALSE;
    }
    switch (id) {
    case GID_CALPREV:  ChangeCalendarMonth(-1); break;
    case GID_CALNEXT:  ChangeCalendarMonth(1); break;
    case GID_LIST:     DoListClick(); break;
    case GID_PREV:     OpenDay(gDay - 1); break;
    case GID_TODAY:    OpenDay(Today()); break;
    case GID_NEXT:     OpenDay(gDay + 1); break;
    }
    return FALSE;
}

/* Handles the window's pending input; returns TRUE when Diary should quit. */
static BOOL HandleInput(void)
{
    ULONG result;
    UWORD code;

    gRebuilt = FALSE;
    while ((result = DoMethod(gWindowObj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
        BOOL quit = FALSE;

        switch (result & WMHI_CLASSMASK) {
        case WMHI_CLOSEWINDOW:
            quit = DoQuit();
            break;
        case WMHI_MENUPICK:
            quit = DoMenuPick(result & WMHI_MENUMASK);
            break;
        case WMHI_GADGETUP:
            quit = DoGadgetUp(result & WMHI_GADGETMASK);
            break;
        }
        if (quit)
            return TRUE;
        if (gRebuilt || gWindow == NULL)
            break;
    }
    return gWindow == NULL;
}

/* ---------- Start-up ---------- */

static struct Library *OpenClass(const char *name, ULONG version, char *missing)
{
    struct Library *base = OpenLibrary((STRPTR)name, version);

    if (base == NULL) {
        if (missing[0] != 0)
            strcat(missing, ", ");
        strcat(missing, name);
    }
    return base;
}

static BOOL OpenLibraries(void)
{
    char missing[400] = "";

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39);
    if (IntuitionBase == NULL)
        return FALSE;
    GfxBase = (struct GfxBase *)OpenClass("graphics.library", 39, missing);
    AslBase = OpenClass("asl.library", 39, missing);
    DiskfontBase = OpenClass("diskfont.library", 39, missing);
    WindowBase = OpenClass("window.class", 47, missing);
    LayoutBase = OpenClass("gadgets/layout.gadget", 47, missing);
    ButtonBase = OpenClass("gadgets/button.gadget", 47, missing);
    ListBrowserBase = OpenClass("gadgets/listbrowser.gadget", 47, missing);
    ScrollerBase = OpenClass("gadgets/scroller.gadget", 47, missing);
    TextFieldBase = OpenClass("gadgets/texteditor.gadget", 47, missing);
    RequesterBase = OpenClass("requester.class", 47, missing);

    if (missing[0] != 0) {
        char message[500];

        strcpy(message, "Diary needs AmigaOS 3.2. These are missing or too old:\n");
        strcat(message, missing);
        ShowMessage(message);
        return FALSE;
    }
    return TRUE;
}

static void CloseLibraries(void)
{
    struct Library *bases[] = {
        RequesterBase, TextFieldBase, ScrollerBase, ListBrowserBase,
        ButtonBase, LayoutBase, WindowBase, DiskfontBase, AslBase,
        (struct Library *)GfxBase, (struct Library *)IntuitionBase
    };
    unsigned i;

    for (i = 0; i < sizeof(bases) / sizeof(bases[0]); i++)
        if (bases[i] != NULL)
            CloseLibrary(bases[i]);
}

int main(void)
{
    int rc = 20;

    int i;

    NewList(&gListNodes);
    for (i = 1; i <= 31; i++)
        AppendNum(gDayNumbers[i], i, 1);

    if (!OpenLibraries())
        goto done;
    gScreen = LockPubScreen(NULL);
    if (gScreen == NULL)
        goto done;
    gDrawInfo = GetScreenDrawInfo(gScreen);
    if (gDrawInfo == NULL)
        goto done;

    FindEntriesFolder();
    LoadSettings();
    OpenTextFont();
    ScanEntries();
    CopyDays(&gShownDays, &gAllDays);
    gDay = Today();
    MakeLongDate(gDay, gDateText);

    if (!CreateWindowObject()) {
        ShowMessage("Could not open the Diary window.");
        goto done;
    }
    RefreshList();
    LoadEntry();
    rc = 0;

    for (;;) {
        ULONG signals = 0;

        GetAttr(WINDOW_SigMask, gWindowObj, &signals);
        if (Wait(signals | SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) {
            SaveEntry();
            break;
        }
        if (HandleInput())
            break;
    }

done:
    DisposeWindowObject();
    if (ListBrowserBase != NULL)
        FreeListBrowserList(&gListNodes);
    if (gTextFont != NULL)
        CloseFont(gTextFont);
    if (gDrawInfo != NULL)
        FreeScreenDrawInfo(gScreen, gDrawInfo);
    if (gScreen != NULL)
        UnlockPubScreen(NULL, gScreen);
    FreeDays(&gAllDays);
    FreeDays(&gShownDays);
    CloseLibraries();
    return rc;
}
