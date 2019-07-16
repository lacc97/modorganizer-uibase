/*
Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "tutorialcontrol.h"
#include <QCoreApplication>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QDebug>
#include <QFile>
#include <QDir>
#include <QMouseEvent>
#include <QToolBar>
#include <QMenuBar>
#include <QAction>
#include <QGraphicsObject>
#include <QTimer>
#include <QAbstractButton>
#include <QTableWidget>
#include "utility.h"
#include "tutorialmanager.h"
#include "report.h"
#include <boost/scoped_array.hpp>
#include <QImage>
#include <QBitmap>

#include <QMetaObject>
#include <QMetaMethod>
#include <QMetaEnum>
#include <qmetaobject.h>


namespace MOBase {


TutorialControl::TutorialControl(const TutorialControl &reference)
  : QObject(reference.parent())
  , m_TargetControl(reference.m_TargetControl)
  , m_Name(reference.m_Name)
  , m_TutorialView(nullptr)
  , m_Manager(TutorialManager::instance())
  , m_ExpectedTab(0)
  , m_CurrentClickControl(nullptr)
{
}


TutorialControl::TutorialControl(QWidget *targetControl, const QString &name)
  : QObject(nullptr)
  , m_TargetControl(targetControl)
  , m_Name(name)
  , m_TutorialView(nullptr)
  , m_Manager(TutorialManager::instance())
  , m_ExpectedTab(0)
  , m_CurrentClickControl(nullptr)
{
}


TutorialControl::~TutorialControl()
{
  m_Manager.unregisterControl(m_Name);
  finish();
}


void TutorialControl::registerControl()
{
  m_Manager.registerControl(m_Name, this);
}


void TutorialControl::resize(const QSize &size)
{
  if (m_TutorialView != nullptr) {
    m_TutorialView->resize(size.width(), size.height());
  }
}


void TutorialControl::expose(const QString &widgetName, QObject *widget)
{
  m_ExposedObjects.push_back(std::make_pair(widgetName, widget));
}


static QString canonicalPath(const QString &path)
{
#if defined(WIN32)
  boost::scoped_array<wchar_t> buffer(new wchar_t[32768]);
  DWORD res = ::GetShortPathNameW((wchar_t*)path.utf16(), buffer.get(), 32768);
  if (res == 0) {
    return path;
  }
  res = ::GetLongPathNameW(buffer.get(), buffer.get(), 32768);
  if (res == 0) {
    return path;
  }
  return QString::fromWCharArray(buffer.get());
#else
  return QFileInfo(path).canonicalPath();
#endif
}

void TutorialControl::startTutorial(const QString &tutorial)
{
  if (m_TutorialView == nullptr) {
    m_TutorialView = new QQuickWidget(m_TargetControl);
    m_TutorialView->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_TutorialView->setAttribute(Qt::WA_TranslucentBackground);
    m_TutorialView->setAttribute(Qt::WA_AlwaysStackOnTop);
    m_TutorialView->setClearColor(Qt::transparent);
    m_TutorialView->setStyleSheet("background: transparent");
    m_TutorialView->setObjectName("tutorialView");
    m_TutorialView->rootContext()->setContextProperty("manager", &m_Manager);

    QString qmlName = canonicalPath(QCoreApplication::applicationDirPath() + "/tutorials") + "/tutorials_" + m_Name.toLower() + ".qml";
    QUrl qmlSource = QUrl::fromLocalFile(qmlName);

    m_TutorialView->setSource(qmlSource);
    m_TutorialView->resize(m_TargetControl->width(), m_TargetControl->height());
    m_TutorialView->rootContext()->setContextProperty("scriptName", tutorial);
    m_TutorialView->rootContext()->setContextProperty("tutorialControl", this);
    m_TutorialView->rootContext()->setContextProperty("applicationWindow", m_TargetControl);
    m_TutorialView->rootContext()->setContextProperty("organizer", m_Manager.organizerCore());

    for (std::vector<std::pair<QString, QObject*> >::const_iterator iter = m_ExposedObjects.begin();
         iter != m_ExposedObjects.end(); ++iter) {
      m_TutorialView->rootContext()->setContextProperty(iter->first, iter->second);
    }
    m_TutorialView->show();
    m_TutorialView->raise();
    if (!QMetaObject::invokeMethod(m_TutorialView->rootObject(), "init")) {
      reportError(tr("Tutorial failed to start, please check \"mo_interface.log\" for details."));
      m_TutorialView->close();
    }
  }
}

void TutorialControl::lockUI(bool locked)
{
  m_TutorialView->setAttribute(Qt::WA_TransparentForMouseEvents, !locked);

  QMetaObject::invokeMethod(m_TutorialView->rootObject(), "enableBackground", Q_ARG(QVariant, QVariant(locked)));
}

void TutorialControl::simulateClick(int x, int y)
{
  bool wasTransparent = m_TutorialView->testAttribute(Qt::WA_TransparentForMouseEvents);
  if (!wasTransparent) {
    m_TutorialView->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  }
  QWidget *hitControl = m_TargetControl->childAt(x, y);
  QPoint globalPos = m_TargetControl->mapToGlobal(QPoint(x, y));
  QPoint hitPos = hitControl->mapFromGlobal(globalPos);
  QMouseEvent *downEvent= new QMouseEvent(QEvent::MouseButtonPress, hitPos, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QMouseEvent *upEvent= new QMouseEvent(QEvent::MouseButtonRelease, hitPos, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);

  qApp->postEvent(hitControl, (QEvent*)downEvent);
  qApp->postEvent(hitControl, (QEvent*)upEvent);

  if (!wasTransparent) {
    m_TutorialView->setAttribute(Qt::WA_TransparentForMouseEvents, false);
  }
}

QWidget* TutorialControl::getChild(const QString &name)
{
  if (m_TargetControl != nullptr) {
    return m_TargetControl->findChild<QWidget*>(name);
  } else {
    return nullptr;
  }
}

void TutorialControl::finish()
{
  if (m_TutorialView != nullptr) {
    m_TutorialView->deleteLater();
  }
  m_TutorialView = nullptr;
  m_TutorialView = nullptr;
}


QRect TutorialControl::getRect(const QString &widgetName)
{
  if (m_TargetControl != nullptr) {
    QWidget *widget = m_TargetControl->findChild<QWidget*>(widgetName);
    if (widget != nullptr) {
      QRect res = widget->rect();
      QPoint pos = widget->mapTo(m_TargetControl, res.topLeft());
      res.moveTopLeft(pos);
      return res;
    } else {
      qCritical("%s not found", qUtf8Printable(widgetName));
      return QRect();
    }
  } else {
    return QRect();
  }
}


QRect TutorialControl::getActionRect(const QString &widgetName)
{
  if (m_TargetControl != nullptr) {
    QToolBar *toolBar = m_TargetControl->findChild<QToolBar*>("toolBar");
    foreach (QAction *action, toolBar->actions()) {
      if (action->objectName() == widgetName) {
        return toolBar->actionGeometry(action);
      }
    }
  }
  return QRect();
}


QRect TutorialControl::getMenuRect(const QString& widgetName)
{
    if (m_TargetControl != nullptr) {
        QMenuBar* menuBar = m_TargetControl->findChild<QMenuBar*>("menuBar");
        return menuBar->geometry();
    }
    return QRect();
}


void TutorialControl::nextTutorialStepProxy()
{

  if (m_TutorialView != nullptr) {
    QObject *background = m_TutorialView->rootObject();

    QTimer::singleShot(1, background, SLOT(nextStep()));
    lockUI(true);

    bool success = false;
    if (sender()->inherits("QAction")) {
      success = disconnect(sender(), SIGNAL(triggered()),
                           this, SLOT(nextTutorialStepProxy()));
    } else if (sender()->inherits("QMenu")) {
      success = disconnect(sender(), SIGNAL(aboutToShow()),
                           this, SLOT(nextTutorialStepProxy()));
    } else {
      success = disconnect(sender(), SIGNAL(pressed()),
                           this, SLOT(nextTutorialStepProxy()));
    }
    if (!success) {
      qCritical("failed to disconnect tutorial proxy");
    }
  } else {
    qCritical("failed to proceed to next tutorial step");
    finish();
  }
}


void TutorialControl::tabChangedProxy(int selected)
{
  if ((m_TutorialView != nullptr) && (selected == m_ExpectedTab)) {
    QObject *background = m_TutorialView->rootObject();
    QTimer::singleShot(1, background, SLOT(nextStep()));
    lockUI(true);
    if (!disconnect(sender(), SIGNAL(currentChanged(int)),
                    this, SLOT(tabChangedProxy(int)))) {
      qCritical("failed to disconnect tab-changed proxy");
    }
  }
}


bool TutorialControl::waitForAction(const QString &actionName)
{
  if (m_TargetControl != nullptr) {
    QAction *action = m_TargetControl->findChild<QAction*>(actionName);
    if (action == nullptr) {
      qCritical("no action \"%s\" in control \"%s\"",
                qUtf8Printable(actionName), qUtf8Printable(m_Name));
      return false;
    }
    if (action->isEnabled()) {
        if (action->menu() != nullptr) {
            connect(action->menu(), SIGNAL(aboutToShow()), this, SLOT(nextTutorialStepProxy()));
        } else {
            connect(action, SIGNAL(triggered()), this, SLOT(nextTutorialStepProxy()));
        }
        lockUI(false);
        return true;
    } else {
      return false;
    }
  } else {
    return false;
  }
}

bool TutorialControl::waitForButton(const QString &buttonName)
{
  if (m_TargetControl != nullptr) {
    QAbstractButton *button = m_TargetControl->findChild<QAbstractButton*>(buttonName);
    if (button == nullptr) {
      qCritical("no button \"%s\" in control \"%s\"",
                qUtf8Printable(buttonName), qUtf8Printable(m_Name));
      return false;
    }
    if (button->isEnabled()) {
      connect(button, SIGNAL(pressed()), this, SLOT(nextTutorialStepProxy()));
      lockUI(false);
      return true;
    } else {
      return false;
    }
  } else {
    return false;
  }
}

bool TutorialControl::waitForTabOpen(const QString &tabControlName, const QString &tab)
{
    if (m_TargetControl != nullptr) {
        QTabWidget* tabWidget = m_TargetControl->findChild<QTabWidget*>(tabControlName);
        if (tabWidget == nullptr) {
            qCritical("no tab widget \"%s\" in control \"%s\"",
                qUtf8Printable(tabControlName), qUtf8Printable(m_Name));
            return false;
        }
        if (tabWidget->findChild<QWidget*>(tab) == nullptr) {
            qCritical("no widget \"%s\" found in tab widget \"%s\"",
                qUtf8Printable(tab), qUtf8Printable(tabControlName));
            return false;
        }
        int tabIndex = tabWidget->indexOf(tabWidget->findChild<QWidget*>(tab));
        if (tabIndex == -1) {
            qCritical("widget \"%s\" does not appear to be a tab in tab widget \"%s\"",
                qUtf8Printable(tab), qUtf8Printable(tabControlName));
            return false;
        }
        if (tabWidget->isEnabled() && (tabWidget->currentIndex() != tabIndex)) {
            m_ExpectedTab = tabIndex;
            connect(tabWidget, SIGNAL(currentChanged(int)), this, SLOT(tabChangedProxy(int)));
            lockUI(false);
            return true;
        }
        else {
            return false;
        }
    }
    else {
        return false;
    }
}

const QString TutorialControl::getTabName(const QString &tabControlName) {
    if (m_TargetControl != nullptr) {
        QTabWidget* tabWidget = m_TargetControl->findChild<QTabWidget*>(tabControlName);
        if (tabWidget == nullptr) {
            qCritical("no tab widget \"%s\" in control \"%s\"",
                qUtf8Printable(tabControlName), qUtf8Printable(m_Name));
            return QString();
        }
        if (tabWidget->currentIndex() == -1) {
            return QString();
        } else {
            return tabWidget->currentWidget()->objectName();
        }
    } else {
        return QString();
    }
}
} // namespace MOBase
