/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2009 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** Commercial Usage
**
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Commercial License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Nokia.
**
** GNU Lesser General Public License Usage
**
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** If you are unsure which license is appropriate for your use, please
** contact the sales department at http://qt.nokia.com/contact.
**
**************************************************************************/

#include "qt4buildconfiguration.h"
#include "qt4project.h"

using namespace Qt4ProjectManager;
using namespace Qt4ProjectManager::Internal;
using namespace ProjectExplorer;

namespace {
    bool debug = false;
}

namespace {
    const char * const KEY_QT_VERSION_ID = "QtVersionId";
}

Qt4BuildConfiguration::Qt4BuildConfiguration(Qt4Project *pro)
    : BuildConfiguration(pro)
{
    init();
}

Qt4BuildConfiguration::Qt4BuildConfiguration(Qt4BuildConfiguration *source)
    : BuildConfiguration(source)
{
    init();
}

Qt4BuildConfiguration::~Qt4BuildConfiguration()
{

}

void Qt4BuildConfiguration::init()
{
    QtVersionManager *vm = QtVersionManager::instance();
    connect(vm, SIGNAL(defaultQtVersionChanged()),
            this, SLOT(defaultQtVersionChanged()));
    connect(vm, SIGNAL(qtVersionsChanged(QList<int>)),
            this, SLOT(qtVersionsChanged(QList<int>)));
}

Qt4Project *Qt4BuildConfiguration::qt4Project() const
{
    return static_cast<Qt4Project *>(project());
}

ProjectExplorer::Environment Qt4BuildConfiguration::baseEnvironment() const
{
    Environment env = useSystemEnvironment() ? Environment::systemEnvironment() : Environment();
    qtVersion()->addToEnvironment(env);
    ToolChain *tc = toolChain();
    if (tc)
        tc->addToEnvironment(env);
    return env;
}

ProjectExplorer::Environment Qt4BuildConfiguration::environment() const
{
    Environment env = baseEnvironment();
    env.modify(userEnvironmentChanges());
    return env;
}

void Qt4BuildConfiguration::setUseSystemEnvironment(bool b)
{
    if (useSystemEnvironment() == b)
        return;
    setValue("clearSystemEnvironment", !b);
    emit environmentChanged();
}

bool Qt4BuildConfiguration::useSystemEnvironment() const
{
    bool b = !(value("clearSystemEnvironment").isValid()
               && value("clearSystemEnvironment").toBool());
    return b;
}

QList<ProjectExplorer::EnvironmentItem> Qt4BuildConfiguration::userEnvironmentChanges() const
{
    return EnvironmentItem::fromStringList(value("userEnvironmentChanges").toStringList());
}

void Qt4BuildConfiguration::setUserEnvironmentChanges(const QList<ProjectExplorer::EnvironmentItem> &diff)
{
    QStringList list = EnvironmentItem::toStringList(diff);
    if (list == value("userEnvironmentChanges").toStringList())
        return;
    setValue("userEnvironmentChanges", list);
    emit environmentChanged();
}

/// returns the build directory
QString Qt4BuildConfiguration::buildDirectory() const
{
    QString workingDirectory;
    if (value("useShadowBuild").toBool())
        workingDirectory = value("buildDirectory").toString();
    if (workingDirectory.isEmpty())
        workingDirectory = QFileInfo(project()->file()->fileName()).absolutePath();
    return workingDirectory;
}

/// returns whether this is a shadow build configuration or not
/// note, even if shadowBuild() returns true, it might be using the
/// source directory as the shadow build directorys, thus not
/// still be a insource build
bool Qt4BuildConfiguration::shadowBuild() const
{
    return value("useShadowBuild").toBool();
}

/// returns the shadow build directory if set
/// \note buildDirectory() is probably the function you want to call
QString Qt4BuildConfiguration::shadowBuildDirectory() const
{
    return value("buildDirectory").toString();
}

void Qt4BuildConfiguration::setShadowBuildAndDirectory(bool shadowBuild, const QString &buildDirectory)
{
    if (value("useShadowBuild").toBool() == shadowBuild
        && value("buildDirectory").toString() == buildDirectory)
        return;
    setValue("useShadowBuild", shadowBuild);
    setValue("buildDirectory", buildDirectory);
    emit buildDirectoryChanged();
    emit targetInformationChanged();
}

ProjectExplorer::ToolChain *Qt4BuildConfiguration::toolChain() const
{
    ToolChain::ToolChainType tct = toolChainType();
    return qtVersion()->toolChain(tct);
}

QString Qt4BuildConfiguration::makeCommand() const
{
    ToolChain *tc = toolChain();
    return tc ? tc->makeCommand() : "make";
}

#ifdef QTCREATOR_WITH_S60
static inline QString symbianMakeTarget(QtVersion::QmakeBuildConfigs buildConfig,
                                        const QString &type)
{
    QString rc = (buildConfig & QtVersion::DebugBuild) ?
                 QLatin1String("debug-") : QLatin1String("release-");
    rc += type;
    return rc;
}
#endif

QString Qt4BuildConfiguration::defaultMakeTarget() const
{
#ifdef QTCREATOR_WITH_S60
    ToolChain *tc = toolChain();
    if (!tc)
        return QString::null;
    const QtVersion::QmakeBuildConfigs buildConfig = qmakeBuildConfiguration();

    switch (tc->type()) {
    case ToolChain::GCCE:
    case ToolChain::GCCE_GNUPOC:
        return symbianMakeTarget(buildConfig, QLatin1String("gcce"));
    case ToolChain::RVCT_ARMV5:
        return symbianMakeTarget(buildConfig, QLatin1String("armv5"));
    case ToolChain::RVCT_ARMV6:
    case ToolChain::RVCT_ARMV6_GNUPOC:
        return symbianMakeTarget(buildConfig, QLatin1String("armv6"));
    default:
        break;
    }
#else

#endif
    return QString::null;
}

QtVersion *Qt4BuildConfiguration::qtVersion() const
{
    return QtVersionManager::instance()->version(qtVersionId());
}

int Qt4BuildConfiguration::qtVersionId() const
{
    QtVersionManager *vm = QtVersionManager::instance();
    if (debug)
        qDebug()<<"Looking for qtVersion ID of "<<displayName();
    int id = 0;
    QVariant vid = value(KEY_QT_VERSION_ID);
    if (vid.isValid()) {
        id = vid.toInt();
        if (vm->version(id)->isValid()) {
            return id;
        } else {
            const_cast<Qt4BuildConfiguration *>(this)->setValue(KEY_QT_VERSION_ID, 0);
            return 0;
        }
    }
    if (debug)
        qDebug()<<"  using qtversion with id ="<<id;
    // Nothing found, reset to default
    const_cast<Qt4BuildConfiguration *>(this)->setValue(KEY_QT_VERSION_ID, id);
    return id;
}

void Qt4BuildConfiguration::setQtVersion(int id)
{
    if (qtVersionId() == id)
        return;

    setValue(KEY_QT_VERSION_ID, id);
    emit qtVersionChanged();
    emit targetInformationChanged();
    emit environmentChanged();
}

void Qt4BuildConfiguration::setToolChainType(ProjectExplorer::ToolChain::ToolChainType type)
{
    if (value("ToolChain").toInt() == type)
        return;
    setValue("ToolChain", (int)type);
    emit toolChainTypeChanged();
    emit targetInformationChanged();
    emit environmentChanged();
}

ProjectExplorer::ToolChain::ToolChainType Qt4BuildConfiguration::toolChainType() const
{
    ToolChain::ToolChainType originalType = ToolChain::ToolChainType(value("ToolChain").toInt());
    ToolChain::ToolChainType type = originalType;
    const QtVersion *version = qtVersion();
    if (!version->possibleToolChainTypes().contains(type)) {
        // Oh no the saved type is not valid for this qt version
        // use default tool chain
        type = version->defaultToolchainType();
        const_cast<Qt4BuildConfiguration *>(this)->setToolChainType(type);
    }
    return type;
}

QtVersion::QmakeBuildConfigs Qt4BuildConfiguration::qmakeBuildConfiguration() const
{
    return QtVersion::QmakeBuildConfigs(value("buildConfiguration").toInt());
}

void Qt4BuildConfiguration::setQMakeBuildConfiguration(QtVersion::QmakeBuildConfigs config)
{
    if (value("buildConfiguration").toInt() == int(config))
        return;
    setValue("buildConfiguration", int(config));
    emit qmakeBuildConfigurationChanged();
    emit targetInformationChanged();
}

void Qt4BuildConfiguration::getConfigCommandLineArguments(QStringList *addedUserConfigs, QStringList *removedUserConfigs) const
{
    QtVersion::QmakeBuildConfigs defaultBuildConfiguration = qtVersion()->defaultBuildConfig();
    QtVersion::QmakeBuildConfigs userBuildConfiguration = qmakeBuildConfiguration();
    if (removedUserConfigs) {
        if ((defaultBuildConfiguration & QtVersion::BuildAll) && !(userBuildConfiguration & QtVersion::BuildAll))
            (*removedUserConfigs) << "debug_and_release";
    }
    if (addedUserConfigs) {
        if (!(defaultBuildConfiguration & QtVersion::BuildAll) && (userBuildConfiguration & QtVersion::BuildAll))
            (*addedUserConfigs) << "debug_and_release";
        if ((defaultBuildConfiguration & QtVersion::DebugBuild) && !(userBuildConfiguration & QtVersion::DebugBuild))
            (*addedUserConfigs) << "release";
        if (!(defaultBuildConfiguration & QtVersion::DebugBuild) && (userBuildConfiguration & QtVersion::DebugBuild))
            (*addedUserConfigs) << "debug";
    }
}

QMakeStep *Qt4BuildConfiguration::qmakeStep() const
{
    QMakeStep *qs = 0;
    foreach(BuildStep *bs, buildSteps())
        if ((qs = qobject_cast<QMakeStep *>(bs)) != 0)
            return qs;
    return 0;
}

MakeStep *Qt4BuildConfiguration::makeStep() const
{
    MakeStep *qs = 0;
    foreach(BuildStep *bs, buildSteps())
        if ((qs = qobject_cast<MakeStep *>(bs)) != 0)
            return qs;
    return 0;
}

void Qt4BuildConfiguration::defaultQtVersionChanged()
{
    if (qtVersionId() == 0) {
        emit qtVersionChanged();
        emit targetInformationChanged();
        emit environmentChanged();
    }
}

void Qt4BuildConfiguration::qtVersionsChanged(const QList<int> &changedVersions)
{
    if (changedVersions.contains(qtVersionId())) {
        if (!qtVersion()->isValid())
            setQtVersion(0);
        emit qtVersionChanged();
        emit targetInformationChanged();
    }
}

// returns true if both are equal
bool Qt4BuildConfiguration::compareToImportFrom(const QString &workingDirectory)
{
    QMakeStep *qs = qmakeStep();
    if (QDir(workingDirectory).exists(QLatin1String("Makefile")) && qs) {
        QString qmakePath = QtVersionManager::findQMakeBinaryFromMakefile(workingDirectory);
        QtVersion *version = qtVersion();
        if (version->qmakeCommand() == qmakePath) {
            // same qtversion
            QPair<QtVersion::QmakeBuildConfigs, QStringList> result =
                    QtVersionManager::scanMakeFile(workingDirectory, version->defaultBuildConfig());
            if (qmakeBuildConfiguration() == result.first) {
                // The QMake Build Configuration are the same,
                // now compare arguments lists
                // we have to compare without the spec/platform cmd argument
                // and compare that on its own
                QString actualSpec = extractSpecFromArgumentList(qs->userArguments(), workingDirectory, version);
                if (actualSpec.isEmpty()) {
                    // Easy one the user has choosen not to override the settings
                    actualSpec = version->mkspec();
                }


                QString parsedSpec = extractSpecFromArgumentList(result.second, workingDirectory, version);
                QStringList actualArgs = removeSpecFromArgumentList(qs->userArguments());
                QStringList parsedArgs = removeSpecFromArgumentList(result.second);

                if (debug) {
                    qDebug()<<"Actual args:"<<actualArgs;
                    qDebug()<<"Parsed args:"<<parsedArgs;
                    qDebug()<<"Actual spec:"<<actualSpec;
                    qDebug()<<"Parsed spec:"<<parsedSpec;
                }

                if (actualArgs == parsedArgs) {
                    // Specs match exactly
                    if (actualSpec == parsedSpec)
                        return true;
                    // Actual spec is the default one
//                    qDebug()<<"AS vs VS"<<actualSpec<<version->mkspec();
                    if ((actualSpec == version->mkspec() || actualSpec == "default")
                        && (parsedSpec == version->mkspec() || parsedSpec == "default" || parsedSpec.isEmpty()))
                        return true;
                }
            }
        }
    }
    return false;
}

// We match -spec and -platfrom separetly
// We ignore -cache, because qmake contained a bug that it didn't
// mention the -cache in the Makefile
// That means changing the -cache option in the additional arguments
// does not automatically rerun qmake. Alas, we could try more
// intelligent matching for -cache, but i guess people rarely
// do use that.

QStringList Qt4BuildConfiguration::removeSpecFromArgumentList(const QStringList &old)
{
    if (!old.contains("-spec") && !old.contains("-platform") && !old.contains("-cache"))
        return old;
    QStringList newList;
    bool ignoreNext = false;
    foreach(const QString &item, old) {
        if (ignoreNext) {
            ignoreNext = false;
        } else if (item == "-spec" || item == "-platform" || item == "-cache") {
            ignoreNext = true;
        } else {
            newList << item;
        }
    }
    return newList;
}

QString Qt4BuildConfiguration::extractSpecFromArgumentList(const QStringList &list, QString directory, QtVersion *version)
{
    int index = list.indexOf("-spec");
    if (index == -1)
        index = list.indexOf("-platform");
    if (index == -1)
        return QString();

    ++index;

    if (index >= list.length())
        return QString();

    QString baseMkspecDir = version->versionInfo().value("QMAKE_MKSPECS");
    if (baseMkspecDir.isEmpty())
        baseMkspecDir = version->versionInfo().value("QT_INSTALL_DATA") + "/mkspecs";

    QString parsedSpec = QDir::cleanPath(list.at(index));
#ifdef Q_OS_WIN
    baseMkspecDir = baseMkspecDir.toLower();
    parsedSpec = parsedSpec.toLower();
#endif
    // if the path is relative it can be
    // relative to the working directory (as found in the Makefiles)
    // or relatively to the mkspec directory
    // if it is the former we need to get the canonical form
    // for the other one we don't need to do anything
    if (QFileInfo(parsedSpec).isRelative()) {
        if(QFileInfo(directory + "/" + parsedSpec).exists()) {
            parsedSpec = QDir::cleanPath(directory + "/" + parsedSpec);
#ifdef Q_OS_WIN
            parsedSpec = parsedSpec.toLower();
#endif
        } else {
            parsedSpec = baseMkspecDir + "/" + parsedSpec;
        }
    }

    QFileInfo f2(parsedSpec);
    while (f2.isSymLink()) {
        parsedSpec = f2.symLinkTarget();
        f2.setFile(parsedSpec);
    }

    if (parsedSpec.startsWith(baseMkspecDir)) {
        parsedSpec = parsedSpec.mid(baseMkspecDir.length() + 1);
    } else {
        QString sourceMkSpecPath = version->sourcePath() + "/mkspecs";
        if (parsedSpec.startsWith(sourceMkSpecPath)) {
            parsedSpec = parsedSpec.mid(sourceMkSpecPath.length() + 1);
        }
    }
#ifdef Q_OS_WIN
    parsedSpec = parsedSpec.toLower();
#endif
    return parsedSpec;
}
