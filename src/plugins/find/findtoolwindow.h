/***************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2008 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact:  Qt Software Information (qt-info@nokia.com)
**
**
** Non-Open Source Usage
**
** Licensees may use this file in accordance with the Qt Beta Version
** License Agreement, Agreement version 2.2 provided with the Software or,
** alternatively, in accordance with the terms contained in a written
** agreement between you and Nokia.
**
** GNU General Public License Usage
**
** Alternatively, this file may be used under the terms of the GNU General
** Public License versions 2.0 or 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the packaging
** of this file.  Please review the following information to ensure GNU
** General Public Licensing requirements will be met:
**
** http://www.fsf.org/licensing/licenses/info/GPLv2.html and
** http://www.gnu.org/copyleft/gpl.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt GPL Exception
** version 1.3, included in the file GPL_EXCEPTION.txt in this package.
**
***************************************************************************/

#ifndef FINDTOOLWINDOW_H
#define FINDTOOLWINDOW_H

#include "ui_finddialog.h"
#include "ifindfilter.h"

#include <QtCore/QList>
#include <QtGui/QCompleter>
#include <QtGui/QWidget>

namespace Find {
namespace Internal {

class FindPlugin;

class FindToolWindow : public QDialog
{
    Q_OBJECT

public:
    FindToolWindow(FindPlugin *plugin);
    ~FindToolWindow();

    void setFindFilters(const QList<IFindFilter *> &filters);

    void setFindText(const QString &text);
    void open(IFindFilter *filter);
    void readSettings();
    void writeSettings();

private slots:
    void search();
    void setCurrentFilter(int index);

private:
    Ui::FindDialog m_ui;
    FindPlugin *m_plugin;
    QList<IFindFilter *> m_filters;
    QCompleter *m_findCompleter;
    QList<QWidget *> m_configWidgets;
};

} // namespace Internal
} // namespace Find

#endif // FINDTOOLWINDOW_H
