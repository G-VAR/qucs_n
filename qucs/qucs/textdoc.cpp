/***************************************************************************
                               textdoc.cpp
                              -------------
    begin                : Sat Mar 11 2006
    copyright            : (C) 2006 by Michael Margraf
    email                : michael.margraf@alumni.tu-berlin.de
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "textdoc.h"
#include "main.h"
#include "qucs.h"

#include <qlabel.h>
#include <qaction.h>
#include <qpixmap.h>
#include <qprinter.h>
#include <qfileinfo.h>
#include <qtabwidget.h>
#include <qmessagebox.h>
#include <qpaintdevicemetrics.h>
#include <qfont.h>


TextDoc::TextDoc(QucsApp *App_, const QString& Name_) : QucsDoc(App_, Name_)
{
  TextFont = QFont("Courier New");
  TextFont.setPointSize(QucsSettings.font.pointSize()-1);
  TextFont.setStyleHint(QFont::Courier);
  TextFont.setFixedPitch(true);
  setFont(TextFont);
  setCurrentFont(TextFont);

  simulation = true;
  Library = "";
  Libraries = "";
  SetChanged = false;

  tmpPosX = tmpPosY = 1;  // set to 1 to trigger line highlighting
  Scale = (float)TextFont.pointSize();
  setUndoDepth(QucsSettings.maxUndo);

  QFileInfo Info(Name_);
  if(App) {
    if(Name_.isEmpty())
      App->DocumentTab->addTab(this, QPixmap(empty_xpm),
                            QObject::tr("untitled"));
    else
      App->DocumentTab->addTab(this, QPixmap(empty_xpm),
                            Info.fileName());
    // calls indirectly "becomeCurrent"
    App->DocumentTab->setCurrentPage(App->DocumentTab->indexOf(this));
    viewport()->setFocus();

    setWordWrap(QTextEdit::NoWrap);
    setPaletteBackgroundColor(QucsSettings.BGColor);
    connect(this, SIGNAL(textChanged()), SLOT(slotSetChanged()));
    connect(this, SIGNAL(cursorPositionChanged(int, int)),
            SLOT(slotCursorPosChanged(int, int)));

    syntaxHighlight = new SyntaxHighlighter(this);
  }
}

TextDoc::~TextDoc()
{
  if(App) {
    delete syntaxHighlight;
    App->DocumentTab->removePage(this);  // delete tab in TabBar
  }
}

// ---------------------------------------------------
bool TextDoc::saveSettings(void)
{
  QFile file(DocName + ".cfg");
  if(!file.open(IO_WriteOnly))
    return false;

  QTextStream stream(&file);

  stream << "VHDL settings file, Qucs " PACKAGE_VERSION "\n"
	 << "Simulation=" << simulation << "\n"
	 << "Duration=" << SimTime << "\n"
	 << "Module=" << (!simulation) << "\n"
	 << "Library=" << Library << "\n"
	 << "Libraries=" << Libraries << "\n";

  file.close();
  SetChanged = false;
  return true;
}

// ---------------------------------------------------
bool TextDoc::loadSettings(void)
{
  QFile file(DocName + ".cfg");
  if(!file.open(IO_ReadOnly))
    return false;

  QTextStream stream(&file);
  QString Line, Setting;

  bool ok;
  while(!stream.atEnd()) {
    Line = stream.readLine();
    Setting = Line.section('=',0,0);
    Line    = Line.section('=',1).stripWhiteSpace();
    if(Setting == "Simulation") {
      simulation = Line.toInt(&ok);
    } else if(Setting == "Duration") {
      SimTime = Line;
    } else if(Setting == "Module") {
    } else if(Setting == "Library") {
      Library = Line;
    } else if(Setting == "Libraries") {
      Libraries = Line;
    }
  }

  file.close();
  return true;
}

// ---------------------------------------------------
void TextDoc::setName(const QString& Name_)
{
  DocName = Name_;

  QFileInfo Info(DocName);
  if (App)
    App->DocumentTab->setTabLabel(this, Info.fileName());

  DataSet = Info.baseName()+".dat";
  DataDisplay = Info.baseName()+".dpl";
}

// ---------------------------------------------------
void TextDoc::becomeCurrent(bool)
{
  int x, y;
  getCursorPosition(&x, &y);
  slotCursorPosChanged(x, y);
  viewport()->setFocus();

  if(isUndoAvailable())  App->undo->setEnabled(true);
  else  App->undo->setEnabled(false);
  if(isRedoAvailable())  App->redo->setEnabled(true);
  else  App->redo->setEnabled(false);
}

// ---------------------------------------------------
void TextDoc::slotCursorPosChanged(int x, int y)
{
  if(tmpPosX > x)
    clearParagraphBackground(tmpPosX);
  else
    for(int z=tmpPosX; z<x; z++)
      clearParagraphBackground(z);
  if(tmpPosX != x)
    setParagraphBackgroundColor(x, QColor(240, 240, 255));
  App->printCursorPosition(x+1, y+1);
  tmpPosX = x;
  tmpPosY = y;
}

// ---------------------------------------------------
void TextDoc::slotSetChanged()
{
  if((isModified() && !DocChanged) || SetChanged) {
    App->DocumentTab->setTabIconSet(this, QPixmap(smallsave_xpm));
    DocChanged = true;
  }
  else if(DocChanged) {
    App->DocumentTab->setTabIconSet(this, QPixmap(empty_xpm));
    DocChanged = false;
  }

  App->undo->setEnabled(isUndoAvailable());
  App->redo->setEnabled(isRedoAvailable());
}

// ---------------------------------------------------
bool TextDoc::load()
{
  QFile file(DocName);
  if(!file.open(IO_ReadOnly))
    return false;

  QTextStream stream(&file);
  setText(stream.read());
  setModified(false);
  slotSetChanged();
  file.close();
  lastSaved = QDateTime::currentDateTime();

  loadSettings();
  return true;
}

// ---------------------------------------------------
int TextDoc::save()
{
  saveSettings();

  QFile file(DocName);
  if(!file.open(IO_WriteOnly))
    return -1;

  QTextStream stream(&file);
  stream << text();
  setModified(false);
  slotSetChanged();
  file.close();

  QFileInfo Info(DocName);
  lastSaved = Info.lastModified();

  return 0;
}

// -----------------------------------------------------------
void TextDoc::print(QPrinter *Printer, QPainter *Painter, bool printAll, bool)
{
  Painter->setFont(TextFont);

  sync();   // formatting whole text

  QPaintDeviceMetrics smetrics(QPainter(this).device());
  QPaintDeviceMetrics pmetrics(Painter->device());
  int margin  = 54;    // margin at each side (unit is point)
  int marginX = margin * pmetrics.logicalDpiX() / smetrics.logicalDpiX();
  int marginY = margin * pmetrics.logicalDpiY() / smetrics.logicalDpiY();
  QRect printArea(
        marginX, marginY, pmetrics.width() - 2*marginX,
        pmetrics.height() - 2*marginY - Painter->fontMetrics().lineSpacing());

  int linesPerPage = printArea.height() / Painter->fontMetrics().lineSpacing();

  int PageCount, PageNo = 1;
  QString s, printText;
  if(printAll) {
    printText = text();
    PageCount = paragraphs();
  }
  else {
    int line1, col1, line2, col2;
    printText = selectedText();
    getSelection(&line1, &col1, &line2, &col2);
    if(line1 < 0) {
      QMessageBox::critical(this, tr("Error"), tr("There is no selection!"));
      return;
    }
    PageCount = line2 - line1 + 1;
  }
  PageCount = (PageCount + linesPerPage - 1) / linesPerPage;
  
  for(;;) {
    if(Printer->aborted())
      break;
    Painter->drawText(printArea, 0, printText.section('\n', 0, linesPerPage-1));
    printText = printText.section('\n', linesPerPage);

    s = tr("Page %1 of %2").arg(PageNo).arg(PageCount);
    Painter->drawText(printArea.right() - Painter->fontMetrics().width(s),
               printArea.bottom() + Painter->fontMetrics().lineSpacing(), s);
    if(printText.isEmpty())
      break;
    Printer->newPage();
    PageNo++;
  }
}

// ---------------------------------------------------
float TextDoc::zoomBy(float s)
{
  if(s < 1.0f) s = -1.0f/s;
  Scale += s;
  if(Scale > 40.0f) Scale = 40.0f;
  if(Scale <  4.0f) Scale =  4.0f;
  zoomTo((int)(Scale+0.5f));
  return Scale;
}

// ---------------------------------------------------
void TextDoc::showAll()
{
  sync();
  Scale *= float(visibleHeight()) / float(contentsHeight());
  if(Scale > 40.0f) Scale = 40.0f;
  if(Scale <  4.0f) Scale =  4.0f;
  zoomTo((int)(Scale+0.5f));
}

// ---------------------------------------------------
void TextDoc::showNoZoom()
{
  Scale = (float)TextFont.pointSize();
  zoomTo(TextFont.pointSize());
}

// ---------------------------------------------------
bool TextDoc::loadSimulationTime(QString& Time)
{
  if(!SimTime.isEmpty()) {
    Time = SimTime;
    return true;
  }

  return false;
}

// ---------------------------------------------------
void TextDoc::outcommmentSelected()
{
  QString s = selectedText();
  if(s.isEmpty())
    return;

  if(s.left(2) == "--")
    s.remove(0, 2);
  else
    s = "--" + s;

  for(int i=s.length()-2; i>=0; i--)
    if(s.at(i) == '\n') {
      if(s.mid(i+1, 2) == "--")
        s.remove(i+1, 2);
      else
        s.insert(i+1, "--");
    }
  insert(s);
}



// *****************************************************************
// **********                                             **********
// **********  The class that do the syntax highlighting  **********
// **********                                             **********
// *****************************************************************

SyntaxHighlighter::SyntaxHighlighter(TextDoc *textEdit) 
                 : QSyntaxHighlighter(textEdit)
{
  Doc = textEdit;
}

SyntaxHighlighter::~SyntaxHighlighter()
{
}

// ---------------------------------------------------
int SyntaxHighlighter::highlightParagraph(const QString& text, int)
{
  QChar c;
  bool isFloat=false;
  int  iString=-1, iWord=-1, iNumber=-1, iExpo=-1, i=0;
  QFont font = Doc->TextFont;
  font.setPointSize((int)Doc->Scale);
  setFormat(0, text.length(), font, QPen::black);

  for(c = text.at(i); !c.isNull(); c = text.at(++i)) {
    if(iString >= 0) {
      setFormat(iString, i-iString+1, QucsSettings.VHDL_String);
      if(c == '"')
        iString = -1;
      continue;
    }
    // ----- word that might become a reserved word --------------
    else if(iWord >= 0) {
      if(c.isLetterOrNumber())
        continue;
      if(c == '_')
        continue;
      do {
        if(iWord > 0)
          if(text.at(iWord-1) == '\'') {
            markAttribute(text, iWord, i-iWord);
            break;
          }
        markWord(text, iWord, i-iWord);
      } while(false);
      iWord = -1;
    }
    // ----- integer or floating point number --------------
    else if(iNumber >= 0) {
      if(c.isNumber())
        continue;
      if(c == '.') {
        if(iExpo < 0) {
          if(isFloat)
            iNumber = -1;
          isFloat = true;
        }
        else
          iNumber = -1;
        continue;
      }
      if((c == 'e') || (c == 'E')) {
        if(iExpo < 0) {
          iExpo = i;
          isFloat = true;
        }
        else
          iNumber = -1;
        continue;
      }
      if((c == '-') || (c == '+')) {
        if((iExpo+1) == i)
          continue;
      }
      if(c != '_')
        if(!c.isLetter()) {
          if(isFloat)
            setFormat(iNumber, i-iNumber, QucsSettings.VHDL_Real);
          else
            setFormat(iNumber, i-iNumber, QucsSettings.VHDL_Integer);
        }
      iNumber = -1;
    }
    // ----- maybe a comment -------------------------------
    else if(c == '-') {
      if(i > 0)
        if(text.at(i-1) == '-') {  // VHDL comment starts with --
          setFormat(i-1, text.length()-i, QucsSettings.VHDL_Comment);
          return 0;
        }
      continue;
    }
    // ----- no special syntax yet (or anymore) --------------
    else {
      if(c.isLetter())
        iWord = i;     // start a word
      else if(c.isNumber()) {
        iExpo = -1;
        iNumber = i;   // start a number
        isFloat = false;
        c = text.at(i-1);
/*        if((c == '-') || (c == '+'))  // include sign into number
          iNumber--;*/
      }
    }

    if(c == '\'') {
      if(i > 1)
        if(text.at(i-2) == '\'')
          setFormat(i-2, 3, QucsSettings.VHDL_Character);
    }
    else if(c == '"')
      iString = i;
  }
  return 0;
}


// ---------------------------------------------------
// reserved VHDL words in alphabetical order
typedef const char*  pChar;
typedef const char** ppChar;
pChar List_A[] = {"abs", "access", "after", "alias", "all", "and",
                  "architecture", "array", "assert", "attribute", 0};
pChar List_B[] = {"begin", "block", "body", "buffer", "bus", 0};
pChar List_C[] = {"case", "component", "configuration", "constant", 0};
pChar List_D[] = {"disconnect", "downto", 0};
pChar List_E[] = {"else", "elsif", "end", "entity", "exit", 0};
pChar List_F[] = {"file", "for", "function", 0};
pChar List_G[] = {"generate", "generic", "group", "guarded", 0};
pChar List_I[] = {"if", "impure", "in", "inertial", "inout", "is", 0};
pChar List_L[] = {"label", "library", "linkage", "literal", "loop", 0};
pChar List_M[] = {"map", "mod", 0};
pChar List_N[] = {"nand", "new", "next", "nor", "not", "null", 0};
pChar List_O[] = {"of", "on", "open", "or", "others", "out", 0};
pChar List_P[] = {"package", "port", "postponed", "procedure", "process",
                  "pure", 0};
pChar List_R[] = {"range", "record", "register", "reject", "rem", "report",
                  "return", "rol", "ror", 0};
pChar List_S[] = {"select", "severity", "shared", "signal", "sla", "sll",
                  "sra", "srl", "subtype", 0};
pChar List_T[] = {"then", "to", "transport", "type", 0};
pChar List_U[] = {"unaffected", "units", "until", "use", 0};
pChar List_V[] = {"variable", 0};
pChar List_W[] = {"wait", "when", "while", "with", 0};
pChar List_X[] = {"xnor", "xor", 0};

ppChar WordList[] =
  {(ppChar)&List_A, (ppChar)&List_B, (ppChar)&List_C, (ppChar)&List_D,
   (ppChar)&List_E, (ppChar)&List_F, (ppChar)&List_G, 0, (ppChar)&List_I,
   0, 0, (ppChar)&List_L, (ppChar)&List_M, (ppChar)&List_N,
   (ppChar)&List_O, (ppChar)&List_P, 0, (ppChar)&List_R, (ppChar)&List_S,
   (ppChar)&List_T, (ppChar)&List_U, (ppChar)&List_V, (ppChar)&List_W,
   (ppChar)&List_X};

pChar List_Units[] = {"fs", "ps", "ns", "us", "ms", "sec", "min", "hr", 0};

pChar List_DataTypes[] = {
   "bit", "bit_vector", "boolean", "std_logic", "std_logic_vector",
   "std_ulogic", "std_ulogic_vector", "signed", "unsigned", "integer",
   "real", "time", "character", "natural", 0};

// ---------------------------------------------------
void SyntaxHighlighter::markWord(const QString& text, int start, int len)
{
  QString Word = text.mid(start, len).lower();
  int idx = (int)(Word.at(0).latin1() - 'a');
  if(idx < 0 || idx > 23)
    return;
  pChar *List = WordList[idx];

  QFont newFont = Doc->TextFont;
  newFont.setPointSize((int)Doc->Scale);

  if(List)
    for( ; *List != 0; List++)
      if(Word == *List) {
        newFont.setWeight(QFont::Bold);
        setFormat(start, len, newFont);
        return;
      }

  for(List = List_DataTypes; *List != 0; List++)
    if(Word == *List) {
      setFormat(start, len, QucsSettings.VHDL_Types);
      return;
    }

  for(List = List_Units; *List != 0; List++)
    if(Word == *List) {
      newFont.setWeight(QFont::Bold);
      setFormat(start, len, newFont, QucsSettings.VHDL_Real);
      return;
    }
}

// ---------------------------------------------------
pChar List_Attrib_A[] = {"active", "ascending", 0};
pChar List_Attrib_B[] = {"base", 0};
pChar List_Attrib_D[] = {"delayed", 0};
pChar List_Attrib_E[] = {"event", 0};
pChar List_Attrib_H[] = {"high", 0};
pChar List_Attrib_I[] = {"image", 0};
pChar List_Attrib_L[] =
   {"last_active", "last_event", "last_value", "left", "leftof", "length", "low", 0};
pChar List_Attrib_P[] = {"pos", "pred", 0};
pChar List_Attrib_Q[] = {"quiet", 0};
pChar List_Attrib_R[] = {"range", "reverse_range", "right", "rightof", 0};
pChar List_Attrib_S[] = {"stable", "succ", 0};
pChar List_Attrib_T[] = {"transaction", 0};
pChar List_Attrib_V[] = {"val", "value", 0};

ppChar Attribute_List[] =
  {(ppChar)&List_Attrib_A, (ppChar)&List_Attrib_B, 0, (ppChar)&List_Attrib_D,
   (ppChar)&List_Attrib_E, 0, 0, (ppChar)&List_Attrib_H, (ppChar)&List_Attrib_I,
   0, 0, (ppChar)&List_Attrib_L, 0, 0, 0, (ppChar)&List_Attrib_P,
   (ppChar)&List_Attrib_Q, (ppChar)&List_Attrib_R, (ppChar)&List_Attrib_S,
   (ppChar)&List_Attrib_T, 0, (ppChar)&List_Attrib_V};

void SyntaxHighlighter::markAttribute(const QString& text, int start, int len)
{
  QString Word = text.mid(start, len).lower();
  int idx = (int)(Word.at(0).latin1() - 'a');
  if(idx < 0 || idx > 22)
    return;
  pChar *List = Attribute_List[idx];

  if(List)
    for(; *List != 0; List++)
      if(Word == *List) {
        setFormat(start-1, len+1, QucsSettings.VHDL_Attributes);
        return;
      }
}