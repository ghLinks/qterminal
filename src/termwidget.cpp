/***************************************************************************
 *   Copyright (C) 2010 by Petr Vanek                                      *
 *   petr@scribus.info                                                     *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/

#include <QMenu>
#include <QVBoxLayout>
#include <QPainter>
#include <QDesktopServices>
#include <QMessageBox>
#include <QAbstractButton>
#include <QMouseEvent>
#include <cassert>

#ifdef HAVE_QDBUS
    #include <QtDBus/QtDBus>
    #include "termwidgetholder.h"
    #include "terminaladaptor.h"
#endif

#ifdef HAVE_LIBCANBERRA
    #include <canberra.h>
#endif

#include "mainwindow.h"
#include "termwidget.h"
#include "config.h"
#include "properties.h"
#include "qterminalapp.h"

static int TermWidgetCount = 0;


TermWidgetImpl::TermWidgetImpl(TerminalConfig &cfg, QWidget * parent)
    : QTermWidget(0, parent)
#ifdef HAVE_LIBCANBERRA
    , libcanberra_context(nullptr)
#endif
{
    TermWidgetCount++;
    QString name(QStringLiteral("TermWidget_%1"));
    setObjectName(name.arg(TermWidgetCount));

    setFlowControlEnabled(FLOW_CONTROL_ENABLED);
    setFlowControlWarningEnabled(FLOW_CONTROL_WARNING_ENABLED);

    m_hasCommand = cfg.hasCommand();

    propertiesChanged();

    setWorkingDirectory(cfg.getWorkingDirectory());

    QStringList shell = cfg.getShell();
    if (!shell.isEmpty())
    {
        setShellProgram(shell.at(0));
        shell.removeAt(0);
        if (!shell.isEmpty())
            setArgs(shell);
    }

    setEnvironment(QStringList(QStringLiteral("TERM=%1").arg(Properties::Instance()->term)));

    setMotionAfterPasting(Properties::Instance()->m_motionAfterPaste);
    disableBracketedPasteMode(Properties::Instance()->m_disableBracketedPasteMode);

    setContextMenuPolicy(Qt::CustomContextMenu);

    if(Properties::Instance()->swapMouseButtons2and3)
    {
        connect(this, &QWidget::customContextMenuRequested,
                this, &TermWidgetImpl::pasteSelection);
    }
    else
    {
        connect(this, &QWidget::customContextMenuRequested,
                this, &TermWidgetImpl::customContextMenuCall);
    }

    connect(this, &QTermWidget::urlActivated, this, &TermWidgetImpl::activateUrl);
    connect(this, &QTermWidget::bell, this, &TermWidgetImpl::bell);

    startShellProgram();
}

TermWidgetImpl::~TermWidgetImpl()
{
#ifdef HAVE_LIBCANBERRA
    if (libcanberra_context) {
        ca_context_destroy (libcanberra_context);
    }
#endif
}

void TermWidgetImpl::propertiesChanged()
{
    setMargin(Properties::Instance()->terminalMargin);
    setColorScheme(Properties::Instance()->colorScheme);
    setTerminalFont(Properties::Instance()->font);
    setMotionAfterPasting(Properties::Instance()->m_motionAfterPaste);
    disableBracketedPasteMode(Properties::Instance()->m_disableBracketedPasteMode);
    setConfirmMultilinePaste(Properties::Instance()->confirmMultilinePaste);
    setWordCharacters(Properties::Instance()->wordCharacters);
    autoHideMouseAfter(Properties::Instance()->mouseAutoHideDelay);
    setTrimPastedTrailingNewlines(Properties::Instance()->trimPastedTrailingNewlines);
    setTerminalSizeHint(Properties::Instance()->showTerminalSizeHint);

    if (Properties::Instance()->historyLimited)
    {
        setHistorySize(Properties::Instance()->historyLimitedTo);
    }
    else
    {
        // Unlimited history
        setHistorySize(-1);
    }

    setKeyBindings(Properties::Instance()->emulation);
    setTerminalOpacity(1.0 - Properties::Instance()->termTransparency/100.0);
    setTerminalBackgroundImage(Properties::Instance()->backgroundImage);
    setTerminalBackgroundMode(Properties::Instance()->backgroundMode);
    setBidiEnabled(Properties::Instance()->enabledBidiSupport);
    setDrawLineChars(!Properties::Instance()->useFontBoxDrawingChars);
    setBoldIntense(Properties::Instance()->boldIntense);

    /* be consequent with qtermwidget.h here */
    switch(Properties::Instance()->scrollBarPos) {
    case 0:
        setScrollBarPosition(QTermWidget::NoScrollBar);
        break;
    case 1:
        setScrollBarPosition(QTermWidget::ScrollBarLeft);
        break;
    case 2:
    default:
        setScrollBarPosition(QTermWidget::ScrollBarRight);
        break;
    }

    switch(Properties::Instance()->keyboardCursorShape) {
    case 1:
        setKeyboardCursorShape(QTermWidget::KeyboardCursorShape::UnderlineCursor);
        break;
    case 2:
        setKeyboardCursorShape(QTermWidget::KeyboardCursorShape::IBeamCursor);
        break;
    default:
    case 0:
        setKeyboardCursorShape(QTermWidget::KeyboardCursorShape::BlockCursor);
        break;
    }

    setBlinkingCursor(Properties::Instance()->keyboardCursorBlink);

    update();
}

void TermWidgetImpl::customContextMenuCall(const QPoint & pos)
{
    auto mainWindow = findParent<MainWindow>(this);
    if (mainWindow == nullptr)
    {
        return;
    }
    QMenu menu(mainWindow);
    QMap<QString, QAction*> actions = mainWindow->leaseActions();

    QList<QAction*> extraActions = filterActions(pos);
    for (auto& action : extraActions)
    {
        menu.addAction(action);
    }

    if (!actions.isEmpty())
    {
        menu.addSeparator();
    }

    menu.addAction(actions[QStringLiteral(COPY_SELECTION)]);
    menu.addAction(actions[QStringLiteral(PASTE_CLIPBOARD)]);
    menu.addAction(actions[QStringLiteral(PASTE_SELECTION)]);
    menu.addAction(actions[QStringLiteral(ZOOM_IN)]);
    menu.addAction(actions[QStringLiteral(ZOOM_OUT)]);
    menu.addAction(actions[QStringLiteral(ZOOM_RESET)]);
    menu.addSeparator();
    menu.addAction(actions[QStringLiteral(CLEAR_TERMINAL)]);
    menu.addAction(actions[QStringLiteral(SPLIT_HORIZONTAL)]);
    menu.addAction(actions[QStringLiteral(SPLIT_VERTICAL)]);
    // warning TODO/FIXME: disable the action when there is only one terminal
    menu.addAction(actions[QStringLiteral(SUB_COLLAPSE)]);
    menu.addSeparator();
    menu.addAction(actions[QStringLiteral(TOGGLE_MENU)]);
    menu.addAction(actions[QStringLiteral(TOGGLE_BOOKMARKS)]);
    menu.addAction(actions[QStringLiteral(HIDE_WINDOW_BORDERS)]);
    menu.addAction(actions[QStringLiteral(PREFERENCES)]);

    // The disabled actions should be updated before showing the menu because
    // the "Actions" menu of the main window may have not been shown yet.
    mainWindow->updateDisabledActions();

    menu.exec(mapToGlobal(pos));
}

void TermWidgetImpl::zoomIn()
{
    QTermWidget::zoomIn();
// note: do not save zoom here due the #74 Zoom reset option resets font back to Monospace
//    Properties::Instance()->font = getTerminalFont();
//    Properties::Instance()->saveSettings();
}

void TermWidgetImpl::zoomOut()
{
    QTermWidget::zoomOut();
// note: do not save zoom here due the #74 Zoom reset option resets font back to Monospace
//    Properties::Instance()->font = getTerminalFont();
//    Properties::Instance()->saveSettings();
}

void TermWidgetImpl::zoomReset()
{
// note: do not save zoom here due the #74 Zoom reset option resets font back to Monospace
//    Properties::Instance()->font = Properties::Instance()->font;
    setTerminalFont(Properties::Instance()->font);
//    Properties::Instance()->saveSettings();
}

void TermWidgetImpl::activateUrl(const QUrl & url, bool fromContextMenu) {
    if (QApplication::keyboardModifiers() & Qt::ControlModifier || fromContextMenu) {
        QDesktopServices::openUrl(url);
    }
}

void TermWidgetImpl::bell() {
    if (Properties::Instance()->audibleBell) {
#ifdef HAVE_LIBCANBERRA
        if (!libcanberra_context) {
            ca_context_create (&libcanberra_context);
        }
        ca_context_play (libcanberra_context, 0,
                         CA_PROP_EVENT_ID, "bell-terminal",
                         NULL);
#else
        qWarning() << "Bell! But QTerminal is not built with libcanberra, so there's no sound.";
#endif
    }
}

bool TermWidget::eventFilter(QObject * /*obj*/, QEvent * ev)
{
    if (ev->type() == QEvent::MouseButtonPress)
    {
        QMouseEvent *mev = static_cast<QMouseEvent*>(ev);
        if (mev->button() == Qt::MiddleButton)
        {
            if(Properties::Instance()->swapMouseButtons2and3)
            {
                impl()->customContextMenuCall(mev->pos());
            }
            else
            {
                impl()->pasteSelection();
            }
            return true;
        }
    }
    else if ((ev->type() == QEvent::MouseMove
              || ev->type() == QEvent::Enter
              || ev->type() == QEvent::HoverEnter)
             && Properties::Instance()->focusOnMoueOver)
    {
        impl()->setFocus();
    }
    return false;
}

TermWidget::TermWidget(TerminalConfig &cfg, QWidget *parent)
    : QWidget(parent)
    , DBusAddressable(QStringLiteral("/terminals"))
    , m_term(new TermWidgetImpl(cfg, this))
    , m_layout(new QVBoxLayout)
    , m_border(palette().color(QPalette::Window))
{

    #ifdef HAVE_QDBUS
    registerAdapter<TerminalAdaptor, TermWidget>(this);
    #endif

    setFocusProxy(m_term);

    setLayout(m_layout);

    m_layout->addWidget(m_term);
    const auto objs = m_term->children();

    for (QObject *o : objs)
    {
        // Find TerminalDisplay
        if (!o->isWidgetType() || qobject_cast<QWidget*>(o)->isHidden())
        {
            continue;
        }
        o->installEventFilter(this);
    }

    propertiesChanged();

    connect(m_term, &QTermWidget::finished, this, &TermWidget::finished);
    connect(m_term, &QTermWidget::termGetFocus, this, &TermWidget::term_termGetFocus);
    connect(m_term, &QTermWidget::termLostFocus, this, &TermWidget::term_termLostFocus);
    connect(m_term, &QTermWidget::titleChanged, this, [this] { emit termTitleChanged(m_term->title(), m_term->icon()); });
}

void TermWidget::propertiesChanged()
{
    if (Properties::Instance()->highlightCurrentTerminal)
        m_layout->setContentsMargins(2, 2, 2, 2);
    else
        m_layout->setContentsMargins(0, 0, 0, 0);

    m_term->propertiesChanged();
}

void TermWidget::term_termGetFocus()
{
    m_border = palette().color(QPalette::Highlight);
    emit termGetFocus(this);
    update();
}

void TermWidget::term_termLostFocus()
{
    m_border = palette().color(QPalette::Window);
    update();
}

void TermWidget::paintEvent (QPaintEvent *)
{
  if (Properties::Instance()->highlightCurrentTerminal)
    {
      QPainter p(this);
      QPen pen(m_border);
      pen.setWidth(3);
      pen.setBrush(m_border);
      p.setPen(pen);
      p.drawRect(0, 0, width()-1, height()-1);
    }
}

#if HAVE_QDBUS

QDBusObjectPath TermWidget::splitHorizontal(const QHash<QString,QVariant> &termArgs)
{
    TermWidgetHolder *holder = findParent<TermWidgetHolder>(this);
    assert(holder != nullptr);
    TerminalConfig cfg = TerminalConfig::fromDbus(termArgs, this);
    return holder->split(this, Qt::Horizontal, cfg)->getDbusPath();
}

QDBusObjectPath TermWidget::splitVertical(const QHash<QString,QVariant> &termArgs)
{
    TermWidgetHolder *holder = findParent<TermWidgetHolder>(this);
    assert(holder != nullptr);
    TerminalConfig cfg = TerminalConfig::fromDbus(termArgs, this);
    return holder->split(this, Qt::Vertical, cfg)->getDbusPath();
}

QDBusObjectPath TermWidget::getTab()
{
    return findParent<TermWidgetHolder>(this)->getDbusPath();
}

void TermWidget::closeTerminal()
{
    TermWidgetHolder *holder = findParent<TermWidgetHolder>(this);
    holder->splitCollapse(this);
}

void TermWidget::sendText(const QString& text)
{
    if (impl())
    {
        impl()->sendText(text);
    }
}

#endif
