/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "qmakeparsernodes.h"
#include "qmakeproject.h"
#include "qmakeprojectmanager.h"
#include "qmakeprojectmanagerconstants.h"
#include "qmakebuildconfiguration.h"
#include "qmakerunconfigurationfactory.h"

#include <projectexplorer/nodesvisitor.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/fileiconprovider.h>
#include <coreplugin/documentmanager.h>
#include <coreplugin/icore.h>
#include <coreplugin/iversioncontrol.h>
#include <coreplugin/vcsmanager.h>
#include <coreplugin/dialogs/readonlyfilesdialog.h>

#include <extensionsystem/pluginmanager.h>

#include <projectexplorer/buildmanager.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/target.h>
#include <projectexplorer/projecttree.h>
#include <qtsupport/profilereader.h>
#include <qtsupport/qtkitinformation.h>
#include <cpptools/generatedcodemodelsupport.h>

#include <resourceeditor/resourcenode.h>

#include <cpptools/cppmodelmanager.h>
#include <cpptools/cpptoolsconstants.h>

#include <utils/algorithm.h>
#include <utils/fileutils.h>
#include <utils/hostosinfo.h>
#include <utils/qtcprocess.h>
#include <utils/mimetypes/mimedatabase.h>
#include <utils/stringutils.h>
#include <utils/theme/theme.h>
#include <proparser/prowriter.h>
#include <proparser/qmakevfs.h>
#include <proparser/ioutils.h>

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextCodec>
#include <QXmlStreamReader>

#include <QMessageBox>
#include <utils/QtConcurrentTools>

using namespace Core;
using namespace ProjectExplorer;
using namespace Utils;
using namespace QMakeInternal;

// Static cached data in struct QmakeParserNodeStaticData providing information and icons
// for file types and the project. Do some magic via qAddPostRoutine()
// to make sure the icons do not outlive QApplication, triggering warnings on X11.

struct FileTypeDataStorage {
    FileType type;
    const char *typeName;
    const char *icon;
    const char *addFileFilter;
};

static const FileTypeDataStorage fileTypeDataStorage[] = {
    { FileType::Header, QT_TRANSLATE_NOOP("QmakeProjectManager::QmakeParserPriFileNode", "Headers"),
      ProjectExplorer::Constants::FILEOVERLAY_H, "*.h; *.hh; *.hpp; *.hxx;"},
    { FileType::Source, QT_TRANSLATE_NOOP("QmakeProjectManager::QmakeParserPriFileNode", "Sources"),
      ProjectExplorer::Constants::FILEOVERLAY_CPP, "*.c; *.cc; *.cpp; *.cp; *.cxx; *.c++;" },
    { FileType::Form, QT_TRANSLATE_NOOP("QmakeProjectManager::QmakeParserPriFileNode", "Forms"),
      Constants::FILEOVERLAY_UI, "*.ui;" },
    { FileType::StateChart, QT_TRANSLATE_NOOP("QmakeProjectManager::QmakeParserPriFileNode", "State charts"),
      ProjectExplorer::Constants::FILEOVERLAY_SCXML, "*.scxml;" },
    { FileType::Resource, QT_TRANSLATE_NOOP("QmakeProjectManager::QmakeParserPriFileNode", "Resources"),
      ProjectExplorer::Constants::FILEOVERLAY_QRC, "*.qrc;" },
    { FileType::QML, QT_TRANSLATE_NOOP("QmakeProjectManager::QmakeParserPriFileNode", "QML"),
      ProjectExplorer::Constants::FILEOVERLAY_QML, "*.qml;" },
    { FileType::Unknown, QT_TRANSLATE_NOOP("QmakeProjectManager::QmakeParserPriFileNode", "Other files"),
      ProjectExplorer::Constants::FILEOVERLAY_UNKNOWN, "*;" }
};

class QmakeParserNodeStaticData {
public:
    class FileTypeData {
    public:
        FileTypeData(FileType t = FileType::Unknown,
                     const QString &tN = QString(),
                     const QString &aff = QString(),
                     const QIcon &i = QIcon()) :
        type(t), typeName(tN), addFileFilter(aff), icon(i) { }

        FileType type;
        QString typeName;
        QString addFileFilter;
        QIcon icon;
    };

    QmakeParserNodeStaticData();

    QVector<FileTypeData> fileTypeData;
    QIcon projectIcon;
};

static void clearQmakeParserNodeStaticData();

QmakeParserNodeStaticData::QmakeParserNodeStaticData()
{
    // File type data
    const unsigned count = sizeof(fileTypeDataStorage)/sizeof(FileTypeDataStorage);
    fileTypeData.reserve(count);

    // Overlay the SP_DirIcon with the custom icons
    const QSize desiredSize = QSize(16, 16);

    const QPixmap dirPixmap = qApp->style()->standardIcon(QStyle::SP_DirIcon).pixmap(desiredSize);
    for (unsigned i = 0 ; i < count; ++i) {
        const QIcon overlayIcon(QLatin1String(fileTypeDataStorage[i].icon));
        QIcon folderIcon;
        folderIcon.addPixmap(FileIconProvider::overlayIcon(dirPixmap, overlayIcon));
        const QString desc = QCoreApplication::translate("QmakeProjectManager::QmakeParserPriFileNode", fileTypeDataStorage[i].typeName);
        const QString filter = QString::fromUtf8(fileTypeDataStorage[i].addFileFilter);
        fileTypeData.push_back(QmakeParserNodeStaticData::FileTypeData(fileTypeDataStorage[i].type,
                                                                 desc, filter, folderIcon));
    }
    // Project icon
    const QIcon projectBaseIcon(ProjectExplorer::Constants::FILEOVERLAY_QT);
    const QPixmap projectPixmap = FileIconProvider::overlayIcon(dirPixmap, projectBaseIcon);
    projectIcon.addPixmap(projectPixmap);

    qAddPostRoutine(clearQmakeParserNodeStaticData);
}

Q_GLOBAL_STATIC(QmakeParserNodeStaticData, qmakeParserNodeStaticData)

static void clearQmakeParserNodeStaticData()
{
    qmakeParserNodeStaticData()->fileTypeData.clear();
    qmakeParserNodeStaticData()->projectIcon = QIcon();
}

enum { debug = 0 };

using namespace QmakeProjectManager;
using namespace QmakeProjectManager::Internal;

namespace QmakeProjectManager {

uint qHash(Variable key, uint seed) { return ::qHash(static_cast<int>(key), seed); }

namespace Internal {
class QmakeEvalInput
{
public:
    QString projectDir;
    FileName projectFilePath;
    QString buildDirectory;
    QString sysroot;
    QtSupport::ProFileReader *readerExact;
    QtSupport::ProFileReader *readerCumulative;
    QMakeGlobals *qmakeGlobals;
    QMakeVfs *qmakeVfs;
};

class QmakePriFileEvalResult
{
public:
    QStringList folders;
    QSet<FileName> recursiveEnumerateFiles;
    QMap<FileType, QSet<FileName>> foundFiles;
};

class QmakeIncludedPriFile
{
public:
    ProFile *proFile;
    Utils::FileName name;
    QmakePriFileEvalResult result;
    QMap<Utils::FileName, QmakeIncludedPriFile *> children;

    ~QmakeIncludedPriFile()
    {
        qDeleteAll(children);
    }
};

class QmakeEvalResult
{
public:
    enum EvalResultState { EvalAbort, EvalFail, EvalPartial, EvalOk };
    EvalResultState state;
    ProjectType projectType;

    QStringList subProjectsNotToDeploy;
    QSet<FileName> exactSubdirs;
    QmakeIncludedPriFile includedFiles;
    TargetParserInformation targetInformation;
    InstallsParserList installsList;
    QHash<Variable, QStringList> newVarValues;
    QStringList errors;
};

class QmakeParserPriFile : public Core::IDocument
{
public:
    QmakeParserPriFile(QmakeParserPriFileNode *qmakePriFile)
        : IDocument(nullptr), m_priFile(qmakePriFile)
    {
        setId("Qmake.PriFile");
        setMimeType(QLatin1String(QmakeProjectManager::Constants::PROFILE_MIMETYPE));
        setFilePath(m_priFile->filePath());
    }

    ReloadBehavior reloadBehavior(ChangeTrigger state, ChangeType type) const override
    {
        Q_UNUSED(state)
        Q_UNUSED(type)
        return BehaviorSilent;
    }
    bool reload(QString *errorString, ReloadFlag flag, ChangeType type) override
    {
        Q_UNUSED(errorString)
        Q_UNUSED(flag)
        if (type == TypePermissions)
            return true;
        m_priFile->scheduleUpdate();
        return true;
    }

private:
    QmakeParserPriFileNode *m_priFile;
};

class ProParserVirtualFolderNode : public VirtualFolderNode
{
public:
    ProParserVirtualFolderNode(InternalParserNode *node);

    QString displayName() const final { return m_typeName; }
    QString addFileFilter() const final { return m_addFileFilter; }
    QString tooltip() const final { return QString(); }

private:
    QString m_typeName;
    QString m_addFileFilter;
};

struct InternalParserNode
{
    QList<InternalParserNode *> virtualfolders;
    QMap<QString, InternalParserNode *> subnodes;
    FileNameList files;
    FileType type = FileType::Unknown;
    int priority = Node::DefaultVirtualFolderPriority;
    QString displayName;
    QString typeName;
    QString addFileFilter;
    QString fullPath;
    QIcon icon;

    ~InternalParserNode()
    {
        qDeleteAll(virtualfolders);
        qDeleteAll(subnodes);
    }

    // Creates: a tree structure from a list of absolute file paths.
    // Empty directories are compressed into a single entry with a longer path.
    // * project
    //    * /absolute/path
    //       * file1
    //    * relative
    //       * path1
    //          * file1
    //          * file2
    //       * path2
    //          * file1
    // The function first creates a tree that looks like the directory structure, i.e.
    //    * /
    //       * absolute
    //          * path
    // ...
    // and afterwards calls compress() which merges directory nodes with single children, i.e. to
    //    * /absolute/path
    void create(const QString &projectDir, const QSet<FileName> &newFilePaths, FileType type)
    {
        static const QChar separator = QLatin1Char('/');
        const FileName projectDirFileName = FileName::fromString(projectDir);
        foreach (const FileName &file, newFilePaths) {
            FileName fileWithoutPrefix;
            bool isRelative;
            if (file.isChildOf(projectDirFileName)) {
                isRelative = true;
                fileWithoutPrefix = file.relativeChildPath(projectDirFileName);
            } else {
                isRelative = false;
                fileWithoutPrefix = file;
            }
            QStringList parts = fileWithoutPrefix.toString().split(separator, QString::SkipEmptyParts);
            if (!HostOsInfo::isWindowsHost() && !isRelative && parts.count() > 0)
                parts[0].prepend(separator);
            QStringListIterator it(parts);
            InternalParserNode *currentNode = this;
            QString path = (isRelative ? (projectDirFileName.toString() + QLatin1Char('/')) : QString());
            while (it.hasNext()) {
                const QString &key = it.next();
                if (it.hasNext()) { // key is directory
                    path += key;
                    if (!currentNode->subnodes.contains(path)) {
                        InternalParserNode *val = new InternalParserNode;
                        val->type = type;
                        val->fullPath = path;
                        val->displayName = key;
                        currentNode->subnodes.insert(path, val);
                        currentNode = val;
                    } else {
                        currentNode = currentNode->subnodes.value(path);
                    }
                    path += separator;
                } else { // key is filename
                    currentNode->files.append(file);
                }
            }
        }
        this->compress();
    }

    // Removes folder nodes with only a single sub folder in it
    void compress()
    {
        QMap<QString, InternalParserNode*> newSubnodes;
        QMapIterator<QString, InternalParserNode*> i(subnodes);
        while (i.hasNext()) {
            i.next();
            i.value()->compress();
            if (i.value()->files.isEmpty() && i.value()->subnodes.size() == 1) {
                // replace i.value() by i.value()->subnodes.begin()
                QString key = i.value()->subnodes.begin().key();
                InternalParserNode *keep = i.value()->subnodes.value(key);
                keep->displayName = i.value()->displayName + QDir::separator() + keep->displayName;
                newSubnodes.insert(key, keep);
                i.value()->subnodes.clear();
                delete i.value();
            } else {
                newSubnodes.insert(i.key(), i.value());
            }
        }
        subnodes = newSubnodes;
    }

    FolderNode *createFolderNode(InternalParserNode *node)
    {
        FolderNode *newNode = 0;
        if (node->typeName.isEmpty())
            newNode = new FolderNode(FileName::fromString(node->fullPath));
        else
            newNode = new ProParserVirtualFolderNode(node);

        newNode->setDisplayName(node->displayName);
        if (!node->icon.isNull())
            newNode->setIcon(node->icon);
        return newNode;
    }

    // Makes the projectNode's subtree below the given folder match this internal node's subtree
    void addSubFolderContents(FolderNode *folder)
    {
        if (type == FileType::Resource) {
            for (const FileName &file : files) {
                auto vfs = static_cast<QmakeParserPriFileNode *>(folder->parentProjectNode())->m_project->qmakeVfs();
                QString contents;
                // Prefer the cumulative file if it's non-empty, based on the assumption
                // that it contains more "stuff".
                vfs->readVirtualFile(file.toString(), QMakeVfs::VfsCumulative, &contents);
                // If the cumulative evaluation botched the file too much, try the exact one.
                if (contents.isEmpty())
                    vfs->readVirtualFile(file.toString(), QMakeVfs::VfsExact, &contents);
                auto resourceNode = new ResourceEditor::ResourceTopLevelNode(file, contents, folder);
                folder->addNode(resourceNode);
                resourceNode->addInternalNodes();
            }
        } else {
            for (const FileName &file : files)
                folder->addNode(new FileNode(file, type, false));
        }

        // Virtual
        {
            for (InternalParserNode *node : virtualfolders) {
                FolderNode *newNode = createFolderNode(node);
                folder->addNode(newNode);
                node->addSubFolderContents(newNode);
            }
        }
        // Subnodes
        {
            QMap<QString, InternalParserNode *>::const_iterator it = subnodes.constBegin();
            QMap<QString, InternalParserNode *>::const_iterator end = subnodes.constEnd();
            for ( ; it != end; ++it) {
                FolderNode *newNode = createFolderNode(it.value());
                folder->addNode(newNode);
                it.value()->addSubFolderContents(newNode);
            }
        }
    }
};

ProParserVirtualFolderNode::ProParserVirtualFolderNode(InternalParserNode *node)
    : VirtualFolderNode(FileName::fromString(node->fullPath), node->priority),
      m_typeName(node->typeName),
      m_addFileFilter(node->addFileFilter)
{}

} // Internal

/*!
  \class QmakeParserPriFileNode
  Implements abstract ProjectNode class
  */

QmakeParserPriFileNode::QmakeParserPriFileNode(QmakeProject *project,
                                               QmakeParserProFileNode *qmakeProFileNode,
                                               const FileName &filePath)
    : ProjectNode(filePath),
      m_project(project),
      m_qmakeProFileNode(qmakeProFileNode),
      m_projectFilePath(filePath),
      m_projectDir(filePath.toFileInfo().absolutePath())
{
    Q_ASSERT(project);
    m_qmakePriFile = new QmakeParserPriFile(this);
    Core::DocumentManager::addDocument(m_qmakePriFile);

    setDisplayName(filePath.toFileInfo().completeBaseName());
    setIcon(qmakeParserNodeStaticData()->projectIcon);
}

QmakeParserPriFileNode::~QmakeParserPriFileNode()
{
    watchFolders(QSet<QString>());
    delete m_qmakePriFile;
}

void QmakeParserPriFileNode::scheduleUpdate()
{
    QtSupport::ProFileCacheManager::instance()->discardFile(m_projectFilePath.toString());
    m_qmakeProFileNode->scheduleUpdate(QmakeParserProFileNode::ParseLater);
}

QStringList QmakeParserPriFileNode::baseVPaths(QtSupport::ProFileReader *reader, const QString &projectDir, const QString &buildDir)
{
    QStringList result;
    if (!reader)
        return result;
    result += reader->absolutePathValues(QLatin1String("VPATH"), projectDir);
    result << projectDir; // QMAKE_ABSOLUTE_SOURCE_PATH
    result << buildDir;
    result.removeDuplicates();
    return result;
}

QStringList QmakeParserPriFileNode::fullVPaths(const QStringList &baseVPaths, QtSupport::ProFileReader *reader,
                                               const QString &qmakeVariable, const QString &projectDir)
{
    QStringList vPaths;
    if (!reader)
        return vPaths;
    vPaths = reader->absolutePathValues(QLatin1String("VPATH_") + qmakeVariable, projectDir);
    vPaths += baseVPaths;
    vPaths.removeDuplicates();
    return vPaths;
}

QSet<FileName> QmakeParserPriFileNode::recursiveEnumerate(const QString &folder)
{
    QSet<FileName> result;
    QDir dir(folder);
    dir.setFilter(dir.filter() | QDir::NoDotAndDotDot);
    foreach (const QFileInfo &file, dir.entryInfoList()) {
        if (file.isDir() && !file.isSymLink())
            result += recursiveEnumerate(file.absoluteFilePath());
        else if (!Core::EditorManager::isAutoSaveFile(file.fileName()))
            result += FileName(file);
    }
    return result;
}

static QStringList fileListForVar(
        const QHash<QString, QVector<ProFileEvaluator::SourceFile>> &sourceFiles,
        const QString &varName)
{
    const QVector<ProFileEvaluator::SourceFile> &sources = sourceFiles[varName];
    QStringList result;
    result.reserve(sources.size());
    foreach (const ProFileEvaluator::SourceFile &sf, sources)
        result << sf.fileName;
    return result;
}

void QmakeParserPriFileNode::extractSources(
        QHash<const ProFile *, QmakePriFileEvalResult *> proToResult, QmakePriFileEvalResult *fallback,
        QVector<ProFileEvaluator::SourceFile> sourceFiles, FileType type)
{
    foreach (const ProFileEvaluator::SourceFile &source, sourceFiles) {
        QmakePriFileEvalResult *result = proToResult.value(source.proFile);
        if (!result)
            result = fallback;
        result->foundFiles[type].insert(FileName::fromString(source.fileName));
    }
}

void QmakeParserPriFileNode::extractInstalls(
        QHash<const ProFile *, QmakePriFileEvalResult *> proToResult, QmakePriFileEvalResult *fallback,
        const InstallsParserList &installList)
{
    for (const InstallsParserItem &item : installList.items) {
        for (const ProFileEvaluator::SourceFile &source : item.files) {
            auto *result = proToResult.value(source.proFile);
            if (!result)
                result = fallback;
            result->folders << source.fileName;
        }
    }
}

void QmakeParserPriFileNode::processValues(QmakePriFileEvalResult &result)
{
    result.folders.removeDuplicates();

    // Remove non existing items and non folders
    QStringList::iterator it = result.folders.begin();
    while (it != result.folders.end()) {
        QFileInfo fi(*it);
        if (fi.exists()) {
            if (fi.isDir()) {
                // keep directories
                ++it;
            } else {
                // move files directly to recursiveEnumerateFiles
                result.recursiveEnumerateFiles << FileName::fromString(*it);
                it = result.folders.erase(it);
            }
        } else {
            // do remove non exsting stuff
            it = result.folders.erase(it);
        }
    }

    foreach (const QString &folder, result.folders)
        result.recursiveEnumerateFiles += recursiveEnumerate(folder);

    const QVector<QmakeParserNodeStaticData::FileTypeData> &fileTypes = qmakeParserNodeStaticData()->fileTypeData;
    for (int i = 0; i < fileTypes.size(); ++i) {
        FileType type = fileTypes.at(i).type;
        QSet<FileName> &foundFiles = result.foundFiles[type];
        result.recursiveEnumerateFiles.subtract(foundFiles);
        QSet<FileName> newFilePaths = filterFilesProVariables(type, foundFiles);
        newFilePaths += filterFilesRecursiveEnumerata(type, result.recursiveEnumerateFiles);
        foundFiles = newFilePaths;
    }
}

void QmakeParserPriFileNode::update(const Internal::QmakePriFileEvalResult &result)
{
    // add project file node
    if (fileNodes().isEmpty())
        addNode(new FileNode(m_projectFilePath, FileType::Project, false));

    m_recursiveEnumerateFiles = result.recursiveEnumerateFiles;
    watchFolders(result.folders.toSet());

    InternalParserNode contents;
    const QVector<QmakeParserNodeStaticData::FileTypeData> &fileTypes = qmakeParserNodeStaticData()->fileTypeData;
    for (int i = 0; i < fileTypes.size(); ++i) {
        FileType type = fileTypes.at(i).type;
        const QSet<FileName> &newFilePaths = result.foundFiles.value(type);
        // We only need to save this information if
        // we are watching folders
        if (!result.folders.isEmpty())
            m_files[type] = newFilePaths;
        else
            m_files[type].clear();

        if (!newFilePaths.isEmpty()) {
            InternalParserNode *subfolder = new InternalParserNode;
            subfolder->type = type;
            subfolder->icon = fileTypes.at(i).icon;
            subfolder->fullPath = m_projectDir;
            subfolder->typeName = fileTypes.at(i).typeName;
            subfolder->addFileFilter = fileTypes.at(i).addFileFilter;
            subfolder->priority = Node::DefaultVirtualFolderPriority - i;
            subfolder->displayName = fileTypes.at(i).typeName;
            contents.virtualfolders.append(subfolder);
            // create the hierarchy with subdirectories
            subfolder->create(m_projectDir, newFilePaths, type);
        }
    }

    contents.addSubFolderContents(this);
}

void QmakeParserPriFileNode::watchFolders(const QSet<QString> &folders)
{
    QSet<QString> toUnwatch = m_watchedFolders;
    toUnwatch.subtract(folders);

    QSet<QString> toWatch = folders;
    toWatch.subtract(m_watchedFolders);

#if 0 // Enable again!
    if (!toUnwatch.isEmpty())
        m_project->unwatchFolders(toUnwatch.toList(), this);
    if (!toWatch.isEmpty())
        m_project->watchFolders(toWatch.toList(), this);
#endif

    m_watchedFolders = folders;
}

bool QmakeParserPriFileNode::folderChanged(const QString &changedFolder, const QSet<FileName> &newFiles)
{
    //qDebug()<<"########## QmakeParserPriFileNode::folderChanged";
    // So, we need to figure out which files changed.

    QSet<FileName> addedFiles = newFiles;
    addedFiles.subtract(m_recursiveEnumerateFiles);

    QSet<FileName> removedFiles = m_recursiveEnumerateFiles;
    removedFiles.subtract(newFiles);

    foreach (const FileName &file, removedFiles) {
        if (!file.isChildOf(FileName::fromString(changedFolder)))
            removedFiles.remove(file);
    }

    if (addedFiles.isEmpty() && removedFiles.isEmpty())
        return false;

    m_recursiveEnumerateFiles = newFiles;

    // Apply the differences
    // per file type
    const QVector<QmakeParserNodeStaticData::FileTypeData> &fileTypes = qmakeParserNodeStaticData()->fileTypeData;
    for (int i = 0; i < fileTypes.size(); ++i) {
        FileType type = fileTypes.at(i).type;
        QSet<FileName> add = filterFilesRecursiveEnumerata(type, addedFiles);
        QSet<FileName> remove = filterFilesRecursiveEnumerata(type, removedFiles);

        if (!add.isEmpty() || !remove.isEmpty()) {
            // Scream :)
//            qDebug()<<"For type"<<fileTypes.at(i).typeName<<"\n"
//                    <<"added files"<<add<<"\n"
//                    <<"removed files"<<remove;

            m_files[type].unite(add);
            m_files[type].subtract(remove);
        }
    }

    // Now apply stuff
    InternalParserNode contents;
    for (int i = 0; i < fileTypes.size(); ++i) {
        FileType type = fileTypes.at(i).type;
        if (!m_files[type].isEmpty()) {
            InternalParserNode *subfolder = new InternalParserNode;
            subfolder->type = type;
            subfolder->icon = fileTypes.at(i).icon;
            subfolder->fullPath = m_projectDir;
            subfolder->typeName = fileTypes.at(i).typeName;
            subfolder->priority = Node::DefaultVirtualFolderPriority - i;
            subfolder->displayName = fileTypes.at(i).typeName;
            contents.virtualfolders.append(subfolder);
            // create the hierarchy with subdirectories
            subfolder->create(m_projectDir, m_files[type], type);
        }
    }

    contents.addSubFolderContents(this);
    return true;
}

bool QmakeParserPriFileNode::deploysFolder(const QString &folder) const
{
    QString f = folder;
    const QChar slash = QLatin1Char('/');
    if (!f.endsWith(slash))
        f.append(slash);

    foreach (const QString &wf, m_watchedFolders) {
        if (f.startsWith(wf)
            && (wf.endsWith(slash)
                || (wf.length() < f.length() && f.at(wf.length()) == slash)))
            return true;
    }
    return false;
}

QList<RunConfiguration *> QmakeParserPriFileNode::runConfigurations() const
{
    QmakeRunConfigurationFactory *factory = QmakeRunConfigurationFactory::find(m_project->activeTarget());
    if (factory)
        return factory->runConfigurationsForNode(m_project->activeTarget(), this);
    return QList<RunConfiguration *>();
}

QList<QmakeParserPriFileNode *> QmakeParserPriFileNode::subProjectNodesExact() const
{
    QList<QmakeParserPriFileNode *> nodes;
    foreach (ProjectNode *node, projectNodes()) {
        QmakeParserPriFileNode *n = dynamic_cast<QmakeParserPriFileNode *>(node);
        if (n && n->includedInExactParse())
            nodes << n;
    }
    return nodes;
}

QmakeParserProFileNode *QmakeParserPriFileNode::proFileNode() const
{
    return m_qmakeProFileNode;
}

bool QmakeParserPriFileNode::includedInExactParse() const
{
    return m_includedInExactParse;
}

void QmakeParserPriFileNode::setIncludedInExactParse(bool b)
{
    m_includedInExactParse = b;
}

QList<ProjectAction> QmakeParserPriFileNode::supportedActions(Node *node) const
{
    QList<ProjectAction> actions;

    const FolderNode *folderNode = this;
    const QmakeParserProFileNode *proFileNode;
    while (!(proFileNode = dynamic_cast<const QmakeParserProFileNode*>(folderNode)))
        folderNode = folderNode->parentFolderNode();
    Q_ASSERT(proFileNode);

    switch (proFileNode->projectType()) {
    case ProjectType::ApplicationTemplate:
    case ProjectType::StaticLibraryTemplate:
    case ProjectType::SharedLibraryTemplate:
    case ProjectType::AuxTemplate: {
        // TODO: Some of the file types don't make much sense for aux
        // projects (e.g. cpp). It'd be nice if the "add" action could
        // work on a subset of the file types according to project type.

        actions << AddNewFile;
        if (m_recursiveEnumerateFiles.contains(node->filePath()))
            actions << EraseFile;
        else
            actions << RemoveFile;

        bool addExistingFiles = true;
        if (node->nodeType() == NodeType::VirtualFolder) {
            // A virtual folder, we do what the projectexplorer does
            FolderNode *folder = node->asFolderNode();
            if (folder) {
                QStringList list;
                foreach (FolderNode *f, folder->folderNodes())
                    list << f->filePath().toString() + QLatin1Char('/');
                if (deploysFolder(Utils::commonPath(list)))
                    addExistingFiles = false;
            }
        }

        addExistingFiles = addExistingFiles && !deploysFolder(node->filePath().toString());

        if (addExistingFiles)
            actions << AddExistingFile << AddExistingDirectory;

        break;
    }
    case ProjectType::SubDirsTemplate:
        actions << AddSubProject << RemoveSubProject;
        break;
    default:
        break;
    }

    FileNode *fileNode = node->asFileNode();
    if ((fileNode && fileNode->fileType() != FileType::Project)
            || dynamic_cast<ResourceEditor::ResourceTopLevelNode *>(node)) {
        actions << Rename;
        actions << DuplicateFile;
    }


    Target *target = m_project->activeTarget();
    QmakeRunConfigurationFactory *factory = QmakeRunConfigurationFactory::find(target);
    if (factory && !factory->runConfigurationsForNode(target, node).isEmpty())
        actions << HasSubProjectRunConfigurations;

    return actions;
}

bool QmakeParserPriFileNode::canAddSubProject(const QString &proFilePath) const
{
    QFileInfo fi(proFilePath);
    if (fi.suffix() == QLatin1String("pro")
        || fi.suffix() == QLatin1String("pri"))
        return true;
    return false;
}

static QString simplifyProFilePath(const QString &proFilePath)
{
    // if proFilePath is like: _path_/projectName/projectName.pro
    // we simplify it to: _path_/projectName
    QFileInfo fi(proFilePath);
    const QString parentPath = fi.absolutePath();
    QFileInfo parentFi(parentPath);
    if (parentFi.fileName() == fi.completeBaseName())
        return parentPath;
    return proFilePath;
}

bool QmakeParserPriFileNode::addSubProjects(const QStringList &proFilePaths)
{
    FindAllFilesVisitor visitor;
    accept(&visitor);
    const FileNameList &allFiles = visitor.filePaths();

    QStringList uniqueProFilePaths;
    foreach (const QString &proFile, proFilePaths)
        if (!allFiles.contains(FileName::fromString(proFile)))
            uniqueProFilePaths.append(simplifyProFilePath(proFile));

    QStringList failedFiles;
    changeFiles(QLatin1String(Constants::PROFILE_MIMETYPE), uniqueProFilePaths, &failedFiles, AddToProFile);

    return failedFiles.isEmpty();
}

bool QmakeParserPriFileNode::removeSubProjects(const QStringList &proFilePaths)
{
    QStringList failedOriginalFiles;
    changeFiles(QLatin1String(Constants::PROFILE_MIMETYPE), proFilePaths, &failedOriginalFiles, RemoveFromProFile);

    QStringList simplifiedProFiles = Utils::transform(failedOriginalFiles, &simplifyProFilePath);

    QStringList failedSimplifiedFiles;
    changeFiles(QLatin1String(Constants::PROFILE_MIMETYPE), simplifiedProFiles, &failedSimplifiedFiles, RemoveFromProFile);

    return failedSimplifiedFiles.isEmpty();
}

bool QmakeParserPriFileNode::addFiles(const QStringList &filePaths, QStringList *notAdded)
{
    // If a file is already referenced in the .pro file then we don't add them.
    // That ignores scopes and which variable was used to reference the file
    // So it's obviously a bit limited, but in those cases you need to edit the
    // project files manually anyway.

    FindAllFilesVisitor visitor;
    accept(&visitor);
    const FileNameList &allFiles = visitor.filePaths();

    typedef QMap<QString, QStringList> TypeFileMap;
    // Split into lists by file type and bulk-add them.
    TypeFileMap typeFileMap;
    Utils::MimeDatabase mdb;
    foreach (const QString &file, filePaths) {
        const Utils::MimeType mt = mdb.mimeTypeForFile(file);
        typeFileMap[mt.name()] << file;
    }

    QStringList failedFiles;
    foreach (const QString &type, typeFileMap.keys()) {
        const QStringList typeFiles = typeFileMap.value(type);
        QStringList qrcFiles; // the list of qrc files referenced from ui files
        if (type == QLatin1String(ProjectExplorer::Constants::RESOURCE_MIMETYPE)) {
            foreach (const QString &formFile, typeFiles) {
                QStringList resourceFiles = formResources(formFile);
                foreach (const QString &resourceFile, resourceFiles)
                    if (!qrcFiles.contains(resourceFile))
                        qrcFiles.append(resourceFile);
            }
        }

        QStringList uniqueQrcFiles;
        foreach (const QString &file, qrcFiles) {
            if (!allFiles.contains(FileName::fromString(file)))
                uniqueQrcFiles.append(file);
        }

        QStringList uniqueFilePaths;
        foreach (const QString &file, typeFiles) {
            if (!allFiles.contains(FileName::fromString(file)))
                uniqueFilePaths.append(file);
        }

        changeFiles(type, uniqueFilePaths, &failedFiles, AddToProFile);
        if (notAdded)
            *notAdded += failedFiles;
        changeFiles(QLatin1String(ProjectExplorer::Constants::RESOURCE_MIMETYPE), uniqueQrcFiles, &failedFiles, AddToProFile);
        if (notAdded)
            *notAdded += failedFiles;
    }
    return failedFiles.isEmpty();
}

bool QmakeParserPriFileNode::removeFiles(const QStringList &filePaths,
                              QStringList *notRemoved)
{
    QStringList failedFiles;
    typedef QMap<QString, QStringList> TypeFileMap;
    // Split into lists by file type and bulk-add them.
    TypeFileMap typeFileMap;
    Utils::MimeDatabase mdb;
    foreach (const QString &file, filePaths) {
        const Utils::MimeType mt = mdb.mimeTypeForFile(file);
        typeFileMap[mt.name()] << file;
    }
    foreach (const QString &type, typeFileMap.keys()) {
        const QStringList typeFiles = typeFileMap.value(type);
        changeFiles(type, typeFiles, &failedFiles, RemoveFromProFile);
        if (notRemoved)
            *notRemoved = failedFiles;
    }
    return failedFiles.isEmpty();
}

bool QmakeParserPriFileNode::deleteFiles(const QStringList &filePaths)
{
    removeFiles(filePaths);
    return true;
}

bool QmakeParserPriFileNode::canRenameFile(const QString &filePath, const QString &newFilePath)
{
    if (newFilePath.isEmpty())
        return false;

    bool changeProFileOptional = deploysFolder(QFileInfo(filePath).absolutePath());
    if (changeProFileOptional)
        return true;
    Utils::MimeDatabase mdb;
    const Utils::MimeType mt = mdb.mimeTypeForFile(newFilePath);

    return renameFile(filePath, newFilePath, mt.name(), Change::TestOnly);
}

bool QmakeParserPriFileNode::renameFile(const QString &filePath, const QString &newFilePath)
{
    if (newFilePath.isEmpty())
        return false;

    bool changeProFileOptional = deploysFolder(QFileInfo(filePath).absolutePath());
    Utils::MimeDatabase mdb;
    const Utils::MimeType mt = mdb.mimeTypeForFile(newFilePath);

    if (renameFile(filePath, newFilePath, mt.name()))
        return true;
    return changeProFileOptional;
}

FolderNode::AddNewInformation QmakeParserPriFileNode::addNewInformation(const QStringList &files, Node *context) const
{
    Q_UNUSED(files)
    return FolderNode::AddNewInformation(filePath().fileName(), context && context->parentProjectNode() == this ? 120 : 90);
}

bool QmakeParserPriFileNode::priFileWritable(const QString &path)
{
    ReadOnlyFilesDialog roDialog(path, ICore::mainWindow());
    roDialog.setShowFailWarning(true);
    return roDialog.exec() != ReadOnlyFilesDialog::RO_Cancel;
}

bool QmakeParserPriFileNode::saveModifiedEditors()
{
    Core::IDocument *document
            = Core::DocumentModel::documentForFilePath(m_projectFilePath.toString());
    if (!document || !document->isModified())
        return true;

    if (!Core::DocumentManager::saveDocument(document))
        return false;

    // force instant reload of ourselves
    QtSupport::ProFileCacheManager::instance()->discardFile(m_projectFilePath.toString());
    m_project->projectManager()->notifyChanged(m_projectFilePath);
    return true;
}

QStringList QmakeParserPriFileNode::formResources(const QString &formFile) const
{
    QStringList resourceFiles;
    QFile file(formFile);
    if (!file.open(QIODevice::ReadOnly))
        return resourceFiles;

    QXmlStreamReader reader(&file);

    QFileInfo fi(formFile);
    QDir formDir = fi.absoluteDir();
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            if (reader.name() == QLatin1String("iconset")) {
                const QXmlStreamAttributes attributes = reader.attributes();
                if (attributes.hasAttribute(QLatin1String("resource")))
                    resourceFiles.append(QDir::cleanPath(formDir.absoluteFilePath(
                                  attributes.value(QLatin1String("resource")).toString())));
            } else if (reader.name() == QLatin1String("include")) {
                const QXmlStreamAttributes attributes = reader.attributes();
                if (attributes.hasAttribute(QLatin1String("location")))
                    resourceFiles.append(QDir::cleanPath(formDir.absoluteFilePath(
                                  attributes.value(QLatin1String("location")).toString())));

            }
        }
    }

    if (reader.hasError())
        qWarning() << "Could not read form file:" << formFile;

    return resourceFiles;
}

bool QmakeParserPriFileNode::ensureWriteableProFile(const QString &file)
{
    // Ensure that the file is not read only
    QFileInfo fi(file);
    if (!fi.isWritable()) {
        // Try via vcs manager
        Core::IVersionControl *versionControl = Core::VcsManager::findVersionControlForDirectory(fi.absolutePath());
        if (!versionControl || !versionControl->vcsOpen(file)) {
            bool makeWritable = QFile::setPermissions(file, fi.permissions() | QFile::WriteUser);
            if (!makeWritable) {
                QMessageBox::warning(Core::ICore::mainWindow(),
                                     QCoreApplication::translate("QmakeParserPriFileNode", "Failed"),
                                     QCoreApplication::translate("QmakeParserPriFileNode", "Could not write project file %1.").arg(file));
                return false;
            }
        }
    }
    return true;
}

QPair<ProFile *, QStringList> QmakeParserPriFileNode::readProFile(const QString &file)
{
    QStringList lines;
    ProFile *includeFile = 0;
    {
        QString contents;
        {
            FileReader reader;
            if (!reader.fetch(file, QIODevice::Text)) {
                QmakeProject::proFileParseError(reader.errorString());
                return qMakePair(includeFile, lines);
            }
            const QTextCodec *codec = Core::EditorManager::defaultTextCodec();
            contents = codec->toUnicode(reader.data());
            lines = contents.split(QLatin1Char('\n'));
        }

        QMakeVfs vfs;
        QtSupport::ProMessageHandler handler;
        QMakeParser parser(0, &vfs, &handler);
        includeFile = parser.parsedProBlock(QStringRef(&contents), file, 1);
    }
    return qMakePair(includeFile, lines);
}

bool QmakeParserPriFileNode::prepareForChange()
{
    return saveModifiedEditors() && ensureWriteableProFile(m_projectFilePath.toString());
}

bool QmakeParserPriFileNode::renameFile(const QString &oldName,
                                  const QString &newName,
                                  const QString &mimeType,
                                  Change mode)
{
    if (!prepareForChange())
        return false;

    QPair<ProFile *, QStringList> pair = readProFile(m_projectFilePath.toString());
    ProFile *includeFile = pair.first;
    QStringList lines = pair.second;

    if (!includeFile)
        return false;

    QDir priFileDir = QDir(m_qmakeProFileNode->m_projectDir);
    QStringList notChanged = ProWriter::removeFiles(includeFile, &lines, priFileDir,
                                                    QStringList(oldName), varNamesForRemoving());

    includeFile->deref();
    if (!notChanged.isEmpty())
        return false;

    // We need to re-parse here: The file has changed.
    QMakeParser parser(0, 0, 0);
    QString contents = lines.join(QLatin1Char('\n'));
    includeFile = parser.parsedProBlock(QStringRef(&contents),
                                        m_projectFilePath.toString(), 1, QMakeParser::FullGrammar);
    QTC_ASSERT(includeFile, return false); // The file should still be valid after what we did.

    ProWriter::addFiles(includeFile, &lines,
                        QStringList(newName),
                        varNameForAdding(mimeType));
    if (mode == Change::Save)
        save(lines);
    includeFile->deref();
    return true;
}

void QmakeParserPriFileNode::changeFiles(const QString &mimeType,
                                 const QStringList &filePaths,
                                 QStringList *notChanged,
                                 ChangeType change, Change mode)
{
    if (filePaths.isEmpty())
        return;

    *notChanged = filePaths;

    // Check for modified editors
    if (!prepareForChange())
        return;

    QPair<ProFile *, QStringList> pair = readProFile(m_projectFilePath.toString());
    ProFile *includeFile = pair.first;
    QStringList lines = pair.second;

    if (!includeFile)
        return;

    if (change == AddToProFile) {
        // Use the first variable for adding.
        ProWriter::addFiles(includeFile, &lines, filePaths, varNameForAdding(mimeType));
        notChanged->clear();
    } else { // RemoveFromProFile
        QDir priFileDir = QDir(m_qmakeProFileNode->m_projectDir);
        *notChanged = ProWriter::removeFiles(includeFile, &lines, priFileDir, filePaths, varNamesForRemoving());
    }

    // save file
    if (mode == Change::Save)
        save(lines);
    includeFile->deref();
}

bool QmakeParserPriFileNode::setProVariable(const QString &var, const QStringList &values, const QString &scope, int flags)
{
    if (!prepareForChange())
        return false;

    QPair<ProFile *, QStringList> pair = readProFile(m_projectFilePath.toString());
    ProFile *includeFile = pair.first;
    QStringList lines = pair.second;

    if (!includeFile)
        return false;

    ProWriter::putVarValues(includeFile, &lines, values, var,
                            ProWriter::PutFlags(flags),
                            scope);

    save(lines);
    includeFile->deref();
    return true;
}

void QmakeParserPriFileNode::save(const QStringList &lines)
{
    {
        FileChangeBlocker changeGuard(m_projectFilePath.toString());
        FileSaver saver(m_projectFilePath.toString(), QIODevice::Text);
        const QTextCodec *codec = Core::EditorManager::defaultTextCodec();
        saver.write(codec->fromUnicode(lines.join(QLatin1Char('\n'))));
        saver.finalize(Core::ICore::mainWindow());
    }

    // This is a hack.
    // We are saving twice in a very short timeframe, once the editor and once the ProFile.
    // So the modification time might not change between those two saves.
    // We manually tell each editor to reload it's file.
    // (The .pro files are notified by the file system watcher.)
    QStringList errorStrings;
    Core::IDocument *document = Core::DocumentModel::documentForFilePath(m_projectFilePath.toString());
    if (document) {
        QString errorString;
        if (!document->reload(&errorString, Core::IDocument::FlagReload, Core::IDocument::TypeContents))
            errorStrings << errorString;
    }
    if (!errorStrings.isEmpty())
        QMessageBox::warning(Core::ICore::mainWindow(), QCoreApplication::translate("QmakeParserPriFileNode", "File Error"),
                             errorStrings.join(QLatin1Char('\n')));
}

QStringList QmakeParserPriFileNode::varNames(FileType type, QtSupport::ProFileReader *readerExact)
{
    QStringList vars;
    switch (type) {
    case FileType::Header:
        vars << QLatin1String("HEADERS");
        vars << QLatin1String("PRECOMPILED_HEADER");
        break;
    case FileType::Source: {
        vars << QLatin1String("SOURCES");
        QStringList listOfExtraCompilers = readerExact->values(QLatin1String("QMAKE_EXTRA_COMPILERS"));
        foreach (const QString &var, listOfExtraCompilers) {
            QStringList inputs = readerExact->values(var + QLatin1String(".input"));
            foreach (const QString &input, inputs)
                // FORMS, RESOURCES, and STATECHARTS are handled below, HEADERS and SOURCES above
                if (input != QLatin1String("FORMS")
                        && input != QLatin1String("STATECHARTS")
                        && input != QLatin1String("RESOURCES")
                        && input != QLatin1String("SOURCES")
                        && input != QLatin1String("HEADERS"))
                    vars << input;
        }
        break;
    }
    case FileType::Resource:
        vars << QLatin1String("RESOURCES");
        break;
    case FileType::Form:
        vars << QLatin1String("FORMS");
        break;
    case FileType::StateChart:
        vars << QLatin1String("STATECHARTS");
        break;
    case FileType::Project:
        vars << QLatin1String("SUBDIRS");
        break;
    case FileType::QML:
        vars << QLatin1String("OTHER_FILES");
        vars << QLatin1String("DISTFILES");
        break;
    default:
        vars << QLatin1String("OTHER_FILES");
        vars << QLatin1String("DISTFILES");
        vars << QLatin1String("ICON");
        vars << QLatin1String("QMAKE_INFO_PLIST");
        break;
    }
    return vars;
}

//!
//! \brief QmakeParserPriFileNode::varNames
//! \param mimeType
//! \return the qmake variable name for the mime type
//! Note: Only used for adding.
//!
QString QmakeParserPriFileNode::varNameForAdding(const QString &mimeType)
{
    if (mimeType == QLatin1String(ProjectExplorer::Constants::CPP_HEADER_MIMETYPE)
            || mimeType == QLatin1String(ProjectExplorer::Constants::C_HEADER_MIMETYPE)) {
        return QLatin1String("HEADERS");
    }

    if (mimeType == QLatin1String(ProjectExplorer::Constants::CPP_SOURCE_MIMETYPE)
               || mimeType == QLatin1String(CppTools::Constants::OBJECTIVE_CPP_SOURCE_MIMETYPE)
               || mimeType == QLatin1String(ProjectExplorer::Constants::C_SOURCE_MIMETYPE)) {
        return QLatin1String("SOURCES");
    }

    if (mimeType == QLatin1String(ProjectExplorer::Constants::RESOURCE_MIMETYPE))
        return QLatin1String("RESOURCES");

    if (mimeType == QLatin1String(ProjectExplorer::Constants::FORM_MIMETYPE))
        return QLatin1String("FORMS");

    if (mimeType == QLatin1String(ProjectExplorer::Constants::QML_MIMETYPE))
        return QLatin1String("DISTFILES");

    if (mimeType == QLatin1String(ProjectExplorer::Constants::SCXML_MIMETYPE))
        return QLatin1String("STATECHARTS");

    if (mimeType == QLatin1String(Constants::PROFILE_MIMETYPE))
        return QLatin1String("SUBDIRS");

    return QLatin1String("DISTFILES");
}

//!
//! \brief QmakeParserPriFileNode::varNamesForRemoving
//! \return all qmake variables which are displayed in the project tree
//! Note: Only used for removing.
//!
QStringList QmakeParserPriFileNode::varNamesForRemoving()
{
    QStringList vars;
    vars << QLatin1String("HEADERS");
    vars << QLatin1String("OBJECTIVE_HEADERS");
    vars << QLatin1String("PRECOMPILED_HEADER");
    vars << QLatin1String("SOURCES");
    vars << QLatin1String("OBJECTIVE_SOURCES");
    vars << QLatin1String("RESOURCES");
    vars << QLatin1String("FORMS");
    vars << QLatin1String("OTHER_FILES");
    vars << QLatin1String("SUBDIRS");
    vars << QLatin1String("DISTFILES");
    vars << QLatin1String("ICON");
    vars << QLatin1String("QMAKE_INFO_PLIST");
    vars << QLatin1String("STATECHARTS");
    return vars;
}

QSet<FileName> QmakeParserPriFileNode::filterFilesProVariables(FileType fileType, const QSet<FileName> &files)
{
    if (fileType != FileType::QML && fileType != FileType::Unknown)
        return files;
    QSet<FileName> result;
    if (fileType == FileType::QML) {
        foreach (const FileName &file, files)
            if (file.toString().endsWith(QLatin1String(".qml")))
                result << file;
    } else {
        foreach (const FileName &file, files)
            if (!file.toString().endsWith(QLatin1String(".qml")))
                result << file;
    }
    return result;
}

QSet<FileName> QmakeParserPriFileNode::filterFilesRecursiveEnumerata(FileType fileType, const QSet<FileName> &files)
{
    QSet<FileName> result;
    if (fileType != FileType::QML && fileType != FileType::Unknown)
        return result;
    if (fileType == FileType::QML) {
        foreach (const FileName &file, files)
            if (file.toString().endsWith(QLatin1String(".qml")))
                result << file;
    } else {
        foreach (const FileName &file, files)
            if (!file.toString().endsWith(QLatin1String(".qml")))
                result << file;
    }
    return result;
}

} // namespace QmakeProjectManager

static QmakeProjectType proFileTemplateTypeToProjectType(ProFileEvaluator::TemplateType type)
{
    switch (type) {
    case ProFileEvaluator::TT_Unknown:
    case ProFileEvaluator::TT_Application:
        return ApplicationTemplate;
    case ProFileEvaluator::TT_StaticLibrary:
        return StaticLibraryTemplate;
    case ProFileEvaluator::TT_SharedLibrary:
        return SharedLibraryTemplate;
    case ProFileEvaluator::TT_Script:
        return ScriptTemplate;
    case ProFileEvaluator::TT_Aux:
        return AuxTemplate;
    case ProFileEvaluator::TT_Subdirs:
        return SubDirsTemplate;
    default:
        return InvalidProject;
    }
}

namespace {
    // feed all files accepted by any of the factories to the callback.
    class FindGeneratorSourcesVisitor : public NodesVisitor
    {
    public:
        FindGeneratorSourcesVisitor(
                const QList<ProjectExplorer::ExtraCompilerFactory *> &factories,
                std::function<void(FileNode *, ProjectExplorer::ExtraCompilerFactory *)> callback) :
            factories(factories), callback(callback) {}

        void visitFolderNode(FolderNode *folderNode) final
        {
            foreach (FileNode *fileNode, folderNode->fileNodes()) {
                foreach (ProjectExplorer::ExtraCompilerFactory *factory, factories) {
                    if (factory->sourceType() == fileNode->fileType())
                        callback(fileNode, factory);
                }
            }
        }

        const QList<ProjectExplorer::ExtraCompilerFactory *> factories;
        std::function<void(FileNode *, ProjectExplorer::ExtraCompilerFactory *)> callback;
    };
}

QmakeParserProFileNode *QmakeParserProFileNode::findProFileFor(const FileName &fileName) const
{
    if (fileName == filePath())
        return const_cast<QmakeParserProFileNode *>(this);
    foreach (ProjectNode *pn, projectNodes())
        if (QmakeParserProFileNode *qmakeProFileNode = dynamic_cast<QmakeParserProFileNode *>(pn))
            if (QmakeParserProFileNode *result = qmakeProFileNode->findProFileFor(fileName))
                return result;
    return 0;
}

QString QmakeParserProFileNode::makefile() const
{
    return singleVariableValue(Variable::Makefile);
}

QString QmakeParserProFileNode::objectExtension() const
{
    if (m_varValues[Variable::ObjectExt].isEmpty())
        return HostOsInfo::isWindowsHost() ? QLatin1String(".obj") : QLatin1String(".o");
    return m_varValues[Variable::ObjectExt].first();
}

QString QmakeParserProFileNode::objectsDirectory() const
{
    return singleVariableValue(Variable::ObjectsDir);
}

QByteArray QmakeParserProFileNode::cxxDefines() const
{
    QByteArray result;
    foreach (const QString &def, variableValue(Variable::Defines)) {
        // 'def' is shell input, so interpret it.
        QtcProcess::SplitError error = QtcProcess::SplitOk;
        const QStringList args = QtcProcess::splitArgs(def, HostOsInfo::hostOs(), false, &error);
        if (error != QtcProcess::SplitOk || args.size() == 0)
            continue;

        result += "#define ";
        const QString defInterpreted = args.first();
        const int index = defInterpreted.indexOf(QLatin1Char('='));
        if (index == -1) {
            result += defInterpreted.toLatin1();
            result += " 1\n";
        } else {
            const QString name = defInterpreted.left(index);
            const QString value = defInterpreted.mid(index + 1);
            result += name.toLatin1();
            result += ' ';
            result += value.toLocal8Bit();
            result += '\n';
        }
    }
    return result;
}

/*!
  \class QmakeParserProFileNode
  Implements abstract ProjectNode class
  */
QmakeParserProFileNode::QmakeParserProFileNode(QmakeProject *project,
                               const FileName &filePath)
        : QmakeParserPriFileNode(project, this, filePath)
{
    // The slot is a lambda, so that QmakeParserProFileNode does not need to be
    // a qobject. The lifetime of the m_parserFutureWatcher is shorter
    // than of this, so this is all safe
    QObject::connect(&m_parseFutureWatcher, &QFutureWatcherBase::finished,
                     [this](){
                         applyAsyncEvaluate();
                     });
}

QmakeParserProFileNode::~QmakeParserProFileNode()
{
    qDeleteAll(m_extraCompilers);
    m_parseFutureWatcher.waitForFinished();
    if (m_readerExact)
        applyAsyncEvaluate();
}

bool QmakeParserProFileNode::isParent(QmakeParserProFileNode *node)
{
    while ((node = dynamic_cast<QmakeParserProFileNode *>(node->parentFolderNode()))) {
        if (node == this)
            return true;
    }
    return false;
}

FolderNode::AddNewInformation QmakeParserProFileNode::addNewInformation(const QStringList &files, Node *context) const
{
    Q_UNUSED(files)
    return AddNewInformation(filePath().fileName(), context && context->parentProjectNode() == this ? 120 : 100);
}

bool QmakeParserProFileNode::isDebugAndRelease() const
{
    const QStringList configValues = m_varValues.value(Variable::Config);
    return configValues.contains(QLatin1String("debug_and_release"));
}

bool QmakeParserProFileNode::isQtcRunnable() const
{
    const QStringList configValues = m_varValues.value(Variable::Config);
    return configValues.contains(QLatin1String("qtc_runnable"));
}

ProjectType QmakeParserProFileNode::projectType() const
{
    return m_projectType;
}

QStringList QmakeParserProFileNode::variableValue(const Variable var) const
{
    return m_varValues.value(var);
}

QString QmakeParserProFileNode::singleVariableValue(const Variable var) const
{
    const QStringList &values = variableValue(var);
    return values.isEmpty() ? QString() : values.first();
}

void QmakeParserProFileNode::setParseInProgressRecursive(bool b)
{
    setParseInProgress(b);
    foreach (ProjectNode *subNode, projectNodes()) {
        if (QmakeParserProFileNode *node = dynamic_cast<QmakeParserProFileNode *>(subNode))
            node->setParseInProgressRecursive(b);
    }
}

void QmakeParserProFileNode::setParseInProgress(bool b)
{
    if (m_parseInProgress == b)
        return;
    m_parseInProgress = b;
#if 0
    emit m_project->proFileUpdated(this, m_validParse, m_parseInProgress);
#endif
}

// Do note the absence of signal emission, always set validParse
// before setParseInProgress, as that will emit the signals
void QmakeParserProFileNode::setValidParseRecursive(bool b)
{
    m_validParse = b;
    foreach (ProjectNode *subNode, projectNodes()) {
        if (QmakeParserProFileNode *node = dynamic_cast<QmakeParserProFileNode *>(subNode))
            node->setValidParseRecursive(b);
    }
}

bool QmakeParserProFileNode::validParse() const
{
    return m_validParse;
}

bool QmakeParserProFileNode::parseInProgress() const
{
    return m_parseInProgress;
}

void QmakeParserProFileNode::scheduleUpdate(QmakeParserProFileNode::AsyncUpdateDelay delay)
{
    setParseInProgressRecursive(true);
#if 0
    m_project->scheduleAsyncUpdate(this, delay);
#endif
}

void QmakeParserProFileNode::asyncUpdate()
{
    m_project->incrementPendingEvaluateFutures();
    setupReader();
    if (!includedInExactParse())
        m_readerExact->setExact(false);
    m_parseFutureWatcher.waitForFinished();
    QmakeEvalInput input = evalInput();
    QFuture<QmakeEvalResult *> future = Utils::runAsync(ProjectExplorerPlugin::sharedThreadPool(),
                                                   QThread::LowestPriority,
                                                   &QmakeParserProFileNode::asyncEvaluate,
                                                   this, input);
    m_parseFutureWatcher.setFuture(future);
}

QmakeEvalInput QmakeParserProFileNode::evalInput() const
{
    QmakeEvalInput input;
    input.projectDir = m_projectDir;
    input.projectFilePath = m_projectFilePath;
    input.buildDirectory = buildDir();
    input.sysroot = m_project->qmakeSysroot();
    input.readerExact = m_readerExact;
    input.readerCumulative = m_readerCumulative;
    input.qmakeGlobals = m_project->qmakeGlobals();
    input.qmakeVfs = m_project->qmakeVfs();
    return input;
}

void QmakeParserProFileNode::setupReader()
{
    Q_ASSERT(!m_readerExact);
    Q_ASSERT(!m_readerCumulative);

#if 0
    m_readerExact = m_project->createProFileReader(this);

    m_readerCumulative = m_project->createProFileReader(this);
    m_readerCumulative->setCumulative(true);
#endif
}

static bool evaluateOne(
        const EvalInput &input, ProFile *pro, QtSupport::ProFileReader *reader,
        bool cumulative, QtSupport::ProFileReader **buildPassReader)
{
#if 0
    if (!reader->accept(pro, QMakeEvaluator::LoadAll))
        return false;

    QStringList builds = reader->values(QLatin1String("BUILDS"));
    if (builds.isEmpty()) {
        *buildPassReader = reader;
    } else {
        QString build = builds.first();
        QHash<QString, QStringList> basevars;
        QStringList basecfgs = reader->values(build + QLatin1String(".CONFIG"));
        basecfgs += build;
        basecfgs += QLatin1String("build_pass");
        basevars[QLatin1String("BUILD_PASS")] = QStringList(build);
        QStringList buildname = reader->values(build + QLatin1String(".name"));
        basevars[QLatin1String("BUILD_NAME")] = (buildname.isEmpty() ? QStringList(build) : buildname);

        // We don't increase/decrease m_qmakeGlobalsRefCnt here, because the outer profilereaders keep m_qmakeGlobals alive anyway
        auto bpReader = new QtSupport::ProFileReader(input.qmakeGlobals, input.qmakeVfs); // needs to access m_qmakeGlobals, m_qmakeVfs
        bpReader->setOutputDir(input.buildDirectory);
        bpReader->setCumulative(cumulative);
        bpReader->setExtraVars(basevars);
        bpReader->setExtraConfigs(basecfgs);

        if (bpReader->accept(pro, QMakeEvaluator::LoadAll))
            *buildPassReader = bpReader;
        else
            delete bpReader;
    }
#endif
    return true;
}

QmakeEvalResult *QmakeParserProFileNode::evaluate(const QmakeEvalInput &input)
{
    QmakeEvalResult *result = new QmakeEvalResult;
#if 0
    QtSupport::ProFileReader *exactBuildPassReader = nullptr;
    QtSupport::ProFileReader *cumulativeBuildPassReader = nullptr;
    ProFile *pro;
    if ((pro = input.readerExact->parsedProFile(input.projectFilePath.toString()))) {
        bool exactOk = evaluateOne(input, pro, input.readerExact, false, &exactBuildPassReader);
        bool cumulOk = evaluateOne(input, pro, input.readerCumulative, true, &cumulativeBuildPassReader);
        pro->deref();
        result->state = exactOk ? ParserEvalResult::EvalOk : cumulOk ? ParserEvalResult::EvalPartial : ParserEvalResult::EvalFail;
    } else {
        result->state = ParserEvalResult::EvalFail;
    }

    if (result->state == ParserEvalResult::EvalFail)
        return result;

    result->includedFiles.proFile = pro;
    result->includedFiles.name = input.projectFilePath;

    QHash<const ProFile *, PriFileEvalResult *> proToResult;

    result->projectType = proFileTemplateTypeToProjectType(
                (result->state == ParserEvalResult::EvalOk ? input.readerExact
                                                     : input.readerCumulative)->templateType());
    if (result->state == ParserEvalResult::EvalOk) {
        if (result->projectType == SubDirsTemplate) {
            QStringList errors;
            FileNameList subDirs = subDirsPaths(input.readerExact, input.projectDir, &result->subProjectsNotToDeploy, &errors);
            result->errors.append(errors);

            foreach (const Utils::FileName &subDirName, subDirs) {
                IncludedPriFile *subDir = new IncludedPriFile;
                subDir->proFile = nullptr;
                subDir->name = subDirName;
                result->includedFiles.children.insert(subDirName, subDir);
            }

            result->exactSubdirs = subDirs.toSet();
        }

        // Convert ProFileReader::includeFiles to IncludedPriFile structure
        QHash<ProFile *, QVector<ProFile *>> includeFiles = input.readerExact->includeFiles();
        QList<IncludedPriFile *> toBuild = { &result->includedFiles };
        while (!toBuild.isEmpty()) {
            IncludedPriFile *current = toBuild.takeFirst();
            if (!current->proFile)
                continue;  // Don't attempt to map subdirs here
            QVector<ProFile *> children = includeFiles.value(current->proFile);
            foreach (ProFile *child, children) {
                const Utils::FileName childName = Utils::FileName::fromString(child->fileName());
                auto it = current->children.find(childName);
                if (it == current->children.end()) {
                    IncludedPriFile *childTree = new IncludedPriFile;
                    childTree->proFile = child;
                    childTree->name = childName;
                    current->children.insert(childName, childTree);
                    proToResult[child] = &childTree->result;
                }
            }
            toBuild.append(current->children.values());
        }
    }

    if (result->projectType == SubDirsTemplate) {
        FileNameList subDirs = subDirsPaths(input.readerCumulative, input.projectDir, 0, 0);
        foreach (const Utils::FileName &subDirName, subDirs) {
            auto it = result->includedFiles.children.find(subDirName);
            if (it == result->includedFiles.children.end()) {
                IncludedPriFile *subDir = new IncludedPriFile;
                subDir->proFile = nullptr;
                subDir->name = subDirName;
                result->includedFiles.children.insert(subDirName, subDir);
            }
        }
    }

    // Add ProFileReader::includeFiles information from cumulative parse to IncludedPriFile structure
    QHash<ProFile *, QVector<ProFile *>> includeFiles = input.readerCumulative->includeFiles();
    QList<IncludedPriFile *> toBuild = { &result->includedFiles };
    while (!toBuild.isEmpty()) {
        IncludedPriFile *current = toBuild.takeFirst();
        if (!current->proFile)
            continue;  // Don't attempt to map subdirs here
        QVector<ProFile *> children = includeFiles.value(current->proFile);
        foreach (ProFile *child, children) {
            const Utils::FileName childName = Utils::FileName::fromString(child->fileName());
            auto it = current->children.find(childName);
            if (it == current->children.end()) {
                IncludedPriFile *childTree = new IncludedPriFile;
                childTree->proFile = child;
                childTree->name = childName;
                current->children.insert(childName, childTree);
                proToResult[child] = &childTree->result;
            }
        }
        toBuild.append(current->children.values());
    }

    auto exactReader = exactBuildPassReader ? exactBuildPassReader : input.readerExact;
    auto cumulativeReader = cumulativeBuildPassReader ? cumulativeBuildPassReader : input.readerCumulative;

    QHash<QString, QVector<ProFileEvaluator::SourceFile>> exactSourceFiles;
    QHash<QString, QVector<ProFileEvaluator::SourceFile>> cumulativeSourceFiles;

    QStringList baseVPathsExact = baseVPaths(exactReader, input.projectDir, input.buildDirectory);
    QStringList baseVPathsCumulative = baseVPaths(cumulativeReader, input.projectDir, input.buildDirectory);

    const QVector<QmakeParserNodeStaticData::FileTypeData> &fileTypes = qmakeNodeStaticData()->fileTypeData;
    for (int i = 0; i < fileTypes.size(); ++i) {
        FileType type = fileTypes.at(i).type;
        QStringList qmakeVariables = varNames(type, exactReader);
        foreach (const QString &qmakeVariable, qmakeVariables) {
            QHash<ProString, bool> handled;
            if (result->state == ParserEvalResult::EvalOk) {
                QStringList vPathsExact = fullVPaths(
                            baseVPathsExact, exactReader, qmakeVariable, input.projectDir);
                auto sourceFiles = exactReader->absoluteFileValues(
                            qmakeVariable, input.projectDir, vPathsExact, &handled);
                exactSourceFiles[qmakeVariable] = sourceFiles;
                extractSources(proToResult, &result->includedFiles.result, sourceFiles, type);
            }
            QStringList vPathsCumulative = fullVPaths(
                        baseVPathsCumulative, cumulativeReader, qmakeVariable, input.projectDir);
            auto sourceFiles = cumulativeReader->absoluteFileValues(
                        qmakeVariable, input.projectDir, vPathsCumulative, &handled);
            cumulativeSourceFiles[qmakeVariable] = sourceFiles;
            extractSources(proToResult, &result->includedFiles.result, sourceFiles, type);
        }
    }

    // This is used for two things:
    // - Actual deployment, in which case we need exact values.
    // - The project tree, in which case we also want exact values to avoid recursively
    //   watching bogus paths. However, we accept the values even if the evaluation
    //   failed, to at least have a best-effort result.
    result->installsList = installsList(exactBuildPassReader, input.projectFilePath.toString(),
                                        input.projectDir, input.buildDirectory);
    extractInstalls(proToResult, &result->includedFiles.result, result->installsList);

    if (result->state == ParserEvalResult::EvalOk) {
        result->targetInformation = targetInformation(input.readerExact, exactBuildPassReader,
                                                      input.buildDirectory, input.projectFilePath.toString());

        // update other variables
        result->newVarValues[Variable::Defines] = exactReader->values(QLatin1String("DEFINES"));
        result->newVarValues[Variable::IncludePath] = includePaths(exactReader, input.sysroot,
                                                            input.buildDirectory, input.projectDir);
        result->newVarValues[Variable::CppFlags] = exactReader->values(QLatin1String("QMAKE_CXXFLAGS"));
        result->newVarValues[Variable::Source] =
                fileListForVar(exactSourceFiles, QLatin1String("SOURCES")) +
                fileListForVar(cumulativeSourceFiles, QLatin1String("SOURCES")) +
                fileListForVar(exactSourceFiles, QLatin1String("HEADERS")) +
                fileListForVar(cumulativeSourceFiles, QLatin1String("HEADERS")) +
                fileListForVar(exactSourceFiles, QLatin1String("OBJECTIVE_HEADERS")) +
                fileListForVar(cumulativeSourceFiles, QLatin1String("OBJECTIVE_HEADERS"));
        result->newVarValues[Variable::UiDir] = QStringList() << uiDirPath(exactReader, input.buildDirectory);
        result->newVarValues[Variable::HeaderExtension] = QStringList() << exactReader->value(QLatin1String("QMAKE_EXT_H"));
        result->newVarValues[Variable::CppExtension] = QStringList() << exactReader->value(QLatin1String("QMAKE_EXT_CPP"));
        result->newVarValues[Variable::MocDir] = QStringList() << mocDirPath(exactReader, input.buildDirectory);
        result->newVarValues[Variable::ExactResource] = fileListForVar(exactSourceFiles, QLatin1String("RESOURCES"));
        result->newVarValues[Variable::CumulativeResource] = fileListForVar(cumulativeSourceFiles, QLatin1String("RESOURCES"));
        result->newVarValues[Variable::PkgConfig] = exactReader->values(QLatin1String("PKGCONFIG"));
        result->newVarValues[Variable::PrecompiledHeader] = ProFileEvaluator::sourcesToFiles(exactReader->fixifiedValues(
                    QLatin1String("PRECOMPILED_HEADER"), input.projectDir, input.buildDirectory));
        result->newVarValues[Variable::LibDirectories] = libDirectories(exactReader);
        result->newVarValues[Variable::Config] = exactReader->values(QLatin1String("CONFIG"));
        result->newVarValues[Variable::QmlImportPath] = exactReader->absolutePathValues(
                    QLatin1String("QML_IMPORT_PATH"), input.projectDir);
        result->newVarValues[Variable::QmlDesignerImportPath] = exactReader->absolutePathValues(
                    QLatin1String("QML_DESIGNER_IMPORT_PATH"), input.projectDir);
        result->newVarValues[Variable::Makefile] = exactReader->values(QLatin1String("MAKEFILE"));
        result->newVarValues[Variable::Qt] = exactReader->values(QLatin1String("QT"));
        result->newVarValues[Variable::ObjectExt] = exactReader->values(QLatin1String("QMAKE_EXT_OBJ"));
        result->newVarValues[Variable::ObjectsDir] = exactReader->values(QLatin1String("OBJECTS_DIR"));
        result->newVarValues[Variable::Version] = exactReader->values(QLatin1String("VERSION"));
        result->newVarValues[Variable::TargetExt] = exactReader->values(QLatin1String("TARGET_EXT"));
        result->newVarValues[Variable::TargetVersionExt]
                = exactReader->values(QLatin1String("TARGET_VERSION_EXT"));
        result->newVarValues[Variable::StaticLibExtension] = exactReader->values(QLatin1String("QMAKE_EXTENSION_STATICLIB"));
        result->newVarValues[Variable::ShLibExtension] = exactReader->values(QLatin1String("QMAKE_EXTENSION_SHLIB"));
        result->newVarValues[Variable::AndroidArch] = exactReader->values(QLatin1String("ANDROID_TARGET_ARCH"));
        result->newVarValues[Variable::AndroidDeploySettingsFile] = exactReader->values(QLatin1String("ANDROID_DEPLOYMENT_SETTINGS_FILE"));
        result->newVarValues[Variable::AndroidPackageSourceDir] = exactReader->values(QLatin1String("ANDROID_PACKAGE_SOURCE_DIR"));
        result->newVarValues[Variable::AndroidExtraLibs] = exactReader->values(QLatin1String("ANDROID_EXTRA_LIBS"));
        result->newVarValues[Variable::IsoIcons] = exactReader->values(QLatin1String("ISO_ICONS"));
        result->newVarValues[Variable::QmakeProjectName] = exactReader->values(QLatin1String("QMAKE_PROJECT_NAME"));
        result->newVarValues[Variable::QmakeCc] = exactReader->values("QMAKE_CC");
        result->newVarValues[Variable::QmakeCxx] = exactReader->values("QMAKE_CXX");
    }

    if (result->state == ParserEvalResult::EvalOk || result->state == ParserEvalResult::EvalPartial) {

        QList<IncludedPriFile *> toExtract = { &result->includedFiles };
        while (!toExtract.isEmpty()) {
            IncludedPriFile *current = toExtract.takeFirst();
            processValues(current->result);
            toExtract.append(current->children.values());
        }
    }

    if (exactBuildPassReader && exactBuildPassReader != input.readerExact)
        delete exactBuildPassReader;
    if (cumulativeBuildPassReader && cumulativeBuildPassReader != input.readerCumulative)
        delete cumulativeBuildPassReader;
#endif
    return result;
}

void QmakeParserProFileNode::asyncEvaluate(QFutureInterface<QmakeEvalResult *> &fi, QmakeEvalInput input)
{
    QmakeEvalResult *evalResult = evaluate(input);
    fi.reportResult(evalResult);
}

void QmakeParserProFileNode::applyAsyncEvaluate()
{
    applyEvaluate(m_parseFutureWatcher.result());
    m_project->decrementPendingEvaluateFutures();
}

bool sortByParserNodes(Node *a, Node *b)
{
    return a->filePath() < b->filePath();
}

void QmakeParserProFileNode::applyEvaluate(QmakeEvalResult *evalResult)
{
    QScopedPointer<QmakeEvalResult> result(evalResult);
    if (!m_readerExact)
        return;

    if (m_project->asyncUpdateState() == QmakeProject::ShuttingDown) {
        cleanupProFileReaders();
        return;
    }

    foreach (const QString &error, evalResult->errors)
        QmakeProject::proFileParseError(error);

    // we are changing what is executed in that case
    if (result->state == QmakeEvalResult::EvalFail || m_project->wasEvaluateCanceled()) {
        m_validParse = false;
        cleanupProFileReaders();
        setValidParseRecursive(false);
        setParseInProgressRecursive(false);

        if (result->state == QmakeEvalResult::EvalFail) {
            QmakeProject::proFileParseError(QCoreApplication::translate("QmakeParserProFileNode", "Error while parsing file %1. Giving up.")
                                            .arg(m_projectFilePath.toUserOutput()));
            if (m_projectType == ProjectType::Invalid)
                return;

            // delete files && folders && projects
            makeEmpty();
            m_projectType = ProjectType::Invalid;
        }
        return;
    }

    if (debug)
        qDebug() << "QmakeParserProFileNode - updating files for file " << m_projectFilePath;

    if (result->projectType != m_projectType) {
        // probably all subfiles/projects have changed anyway
        // delete files && folders && projects
        foreach (ProjectNode *projectNode, projectNodes()) {
            if (QmakeParserProFileNode *qmakeProFileNode = dynamic_cast<QmakeParserProFileNode *>(projectNode)) {
                qmakeProFileNode->setValidParseRecursive(false);
                qmakeProFileNode->setParseInProgressRecursive(false);
            }
        }

        makeEmpty();
        m_projectType = result->projectType;
    }

    //
    // Add/Remove pri files, sub projects
    //

    QString buildDirectory = buildDir();

    QList<QPair<QmakeParserPriFileNode *, QmakeIncludedPriFile *>> toCompare;

    toCompare.append(qMakePair(this, &result->includedFiles));

    makeEmpty();

    while (!toCompare.isEmpty()) {
        QmakeParserPriFileNode *pn = toCompare.first().first;
        QmakeIncludedPriFile *tree = toCompare.first().second;
        toCompare.pop_front();

        for (QmakeIncludedPriFile *priFile : tree->children) {
            // Loop preventation, make sure that exact same node is not in our parent chain
            bool loop = false;
            Node *n = pn;
            while ((n = n->parentFolderNode())) {
                if (dynamic_cast<QmakeParserPriFileNode *>(n) && n->filePath() == priFile->name) {
                    loop = true;
                    break;
                }
            }

            if (loop)
                continue; // Do nothing

            if (priFile->proFile) {
                QmakeParserPriFileNode *qmakePriFileNode = new QmakeParserPriFileNode(m_project, this, priFile->name);
                pn->addNode(qmakePriFileNode);
                qmakePriFileNode->setIncludedInExactParse(
                            (result->state == QmakeEvalResult::EvalOk) && pn->includedInExactParse());
                qmakePriFileNode->update(priFile->result);
                toCompare.append(qMakePair(qmakePriFileNode, priFile));
            } else {
                QmakeParserProFileNode *qmakeProFileNode = new QmakeParserProFileNode(m_project, priFile->name);
                pn->addNode(qmakeProFileNode);
                qmakeProFileNode->setIncludedInExactParse(
                            result->exactSubdirs.contains(qmakeProFileNode->filePath())
                            && pn->includedInExactParse());
                qmakeProFileNode->setParseInProgress(true);
                qmakeProFileNode->asyncUpdate();
            }
        }
    }

    QmakeParserPriFileNode::update(result->includedFiles.result);

    m_validParse = (result->state == QmakeEvalResult::EvalOk);
    if (m_validParse) {
        // update TargetInformation
        m_qmakeTargetInformation = result->targetInformation;

        m_subProjectsNotToDeploy = result->subProjectsNotToDeploy;
        m_installsList = result->installsList;

        if (m_varValues != result->newVarValues)
            m_varValues = result->newVarValues;

        const QString projectName = singleVariableValue(Variable::QmakeProjectName);
        if (projectName.isEmpty())
            setDisplayName(m_projectFilePath.toFileInfo().completeBaseName());
        else
            setDisplayName(projectName);
    } // result == EvalOk

    setParseInProgress(false);

    updateGeneratedFiles(buildDirectory);

    cleanupProFileReaders();
    ProjectNode::emitTreeChanged();
}

void QmakeParserProFileNode::cleanupProFileReaders()
{
    m_project->destroyProFileReader(m_readerExact);
    m_project->destroyProFileReader(m_readerCumulative);

    m_readerExact = nullptr;
    m_readerCumulative = nullptr;
}

QString QmakeParserProFileNode::uiDirPath(QtSupport::ProFileReader *reader, const QString &buildDir)
{
    QString path = reader->value(QLatin1String("UI_DIR"));
    if (QFileInfo(path).isRelative())
        path = QDir::cleanPath(buildDir + QLatin1Char('/') + path);
    return path;
}

QString QmakeParserProFileNode::mocDirPath(QtSupport::ProFileReader *reader, const QString &buildDir)
{
    QString path = reader->value(QLatin1String("MOC_DIR"));
    if (QFileInfo(path).isRelative())
        path = QDir::cleanPath(buildDir + QLatin1Char('/') + path);
    return path;
}

QString QmakeParserProFileNode::sysrootify(const QString &path, const QString &sysroot,
                                     const QString &baseDir, const QString &outputDir)
{
#ifdef Q_OS_WIN
    Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    if (sysroot.isEmpty() || path.startsWith(sysroot, cs)
        || path.startsWith(baseDir, cs) || path.startsWith(outputDir, cs)) {
        return path;
    }
    QString sysrooted = QDir::cleanPath(sysroot + path);
    return !IoUtils::exists(sysrooted) ? path : sysrooted;
}

QStringList QmakeParserProFileNode::includePaths(QtSupport::ProFileReader *reader, const QString &sysroot,
                                           const QString &buildDir, const QString &projectDir)
{
    QStringList paths;
    foreach (const QString &cxxflags, reader->values(QLatin1String("QMAKE_CXXFLAGS"))) {
        if (cxxflags.startsWith(QLatin1String("-I")))
            paths.append(cxxflags.mid(2));
    }

    foreach (const ProFileEvaluator::SourceFile &el,
             reader->fixifiedValues(QLatin1String("INCLUDEPATH"), projectDir, buildDir)) {
        paths << sysrootify(el.fileName, sysroot, projectDir, buildDir);
    }
    // paths already contains moc dir and ui dir, due to corrrectly parsing uic.prf and moc.prf
    // except if those directories don't exist at the time of parsing
    // thus we add those directories manually (without checking for existence)
    paths << mocDirPath(reader, buildDir) << uiDirPath(reader, buildDir);
    paths.removeDuplicates();
    return paths;
}

QStringList QmakeParserProFileNode::libDirectories(QtSupport::ProFileReader *reader)
{
    QStringList result;
    foreach (const QString &str, reader->values(QLatin1String("LIBS"))) {
        if (str.startsWith(QLatin1String("-L")))
            result.append(str.mid(2));
    }
    return result;
}

FileNameList QmakeParserProFileNode::subDirsPaths(QtSupport::ProFileReader *reader,
                                            const QString &projectDir,
                                            QStringList *subProjectsNotToDeploy,
                                            QStringList *errors)
{
    FileNameList subProjectPaths;

    const QStringList subDirVars = reader->values(QLatin1String("SUBDIRS"));

    foreach (const QString &subDirVar, subDirVars) {
        // Special case were subdir is just an identifier:
        //   "SUBDIR = subid
        //    subid.subdir = realdir"
        // or
        //   "SUBDIR = subid
        //    subid.file = realdir/realfile.pro"

        QString realDir;
        const QString subDirKey = subDirVar + QLatin1String(".subdir");
        const QString subDirFileKey = subDirVar + QLatin1String(".file");
        if (reader->contains(subDirKey))
            realDir = reader->value(subDirKey);
        else if (reader->contains(subDirFileKey))
            realDir = reader->value(subDirFileKey);
        else
            realDir = subDirVar;
        QFileInfo info(realDir);
        if (!info.isAbsolute())
            info.setFile(projectDir + QLatin1Char('/') + realDir);
        realDir = info.filePath();

        QString realFile;
        if (info.isDir())
            realFile = QString::fromLatin1("%1/%2.pro").arg(realDir, info.fileName());
        else
            realFile = realDir;

        if (QFile::exists(realFile)) {
            realFile = QDir::cleanPath(realFile);
            subProjectPaths << FileName::fromString(realFile);
            if (subProjectsNotToDeploy && !subProjectsNotToDeploy->contains(realFile)
                    && reader->values(subDirVar + QLatin1String(".CONFIG"))
                        .contains(QLatin1String("no_default_target"))) {
                subProjectsNotToDeploy->append(realFile);
            }
        } else {
            if (errors)
                errors->append(QCoreApplication::translate("QmakeParserProFileNode", "Could not find .pro file for subdirectory \"%1\" in \"%2\".")
                               .arg(subDirVar).arg(realDir));
        }
    }

    return Utils::filteredUnique(subProjectPaths);
}

TargetParserInformation QmakeParserProFileNode::targetInformation(QtSupport::ProFileReader *reader,
                                                                  QtSupport::ProFileReader *readerBuildPass, const QString &buildDir, const QString &projectFilePath)
{
    TargetParserInformation result;
    if (!reader || !readerBuildPass)
        return result;

    QStringList builds = reader->values(QLatin1String("BUILDS"));
    if (!builds.isEmpty()) {
        QString build = builds.first();
        result.buildTarget = reader->value(build + QLatin1String(".target"));
    }

    // BUILD DIR
    result.buildDir = buildDir;

    if (readerBuildPass->contains(QLatin1String("DESTDIR")))
        result.destDir = readerBuildPass->value(QLatin1String("DESTDIR"));

    // Target
    result.target = readerBuildPass->value(QLatin1String("TARGET"));
    if (result.target.isEmpty())
        result.target = QFileInfo(projectFilePath).baseName();

    result.valid = true;

    return result;
}

TargetParserInformation QmakeParserProFileNode::targetInformation() const
{
    return m_qmakeTargetInformation;
}

InstallsParserList QmakeParserProFileNode::installsList(const QtSupport::ProFileReader *reader, const QString &projectFilePath,
                                                        const QString &projectDir, const QString &buildDir)
{
    InstallsParserList result;
    if (!reader)
        return result;
    const QStringList &itemList = reader->values(QLatin1String("INSTALLS"));
    if (itemList.isEmpty())
        return result;

    const QString installPrefix
            = reader->propertyValue(QLatin1String("QT_INSTALL_PREFIX"));
    const QString devInstallPrefix
            = reader->propertyValue(QLatin1String("QT_INSTALL_PREFIX/dev"));
    bool fixInstallPrefix = (installPrefix != devInstallPrefix);

    foreach (const QString &item, itemList) {
        bool active = !reader->values(item + QLatin1String(".CONFIG"))
                        .contains(QLatin1String("no_default_install"));
        const QString pathVar = item + QLatin1String(".path");
        const QStringList &itemPaths = reader->values(pathVar);
        if (itemPaths.count() != 1) {
            qDebug("Invalid RHS: Variable '%s' has %d values.",
                qPrintable(pathVar), itemPaths.count());
            if (itemPaths.isEmpty()) {
                qDebug("%s: Ignoring INSTALLS item '%s', because it has no path.",
                    qPrintable(projectFilePath), qPrintable(item));
                continue;
            }
        }

        QString itemPath = itemPaths.last();
        if (fixInstallPrefix && itemPath.startsWith(installPrefix)) {
            // This is a hack for projects which install into $$[QT_INSTALL_*],
            // in particular Qt itself, examples being most relevant.
            // Projects which implement their own install path policy must
            // parametrize their INSTALLS themselves depending on the intended
            // installation/deployment mode.
            itemPath.replace(0, installPrefix.length(), devInstallPrefix);
        }
        if (item == QLatin1String("target")) {
            if (active)
                result.targetPath = itemPath;
        } else {
            const auto &itemFiles = reader->fixifiedValues(
                        item + QLatin1String(".files"), projectDir, buildDir);
            result.items << InstallsParserItem(itemPath, itemFiles, active);
        }
    }
    return result;
}

InstallsParserList QmakeParserProFileNode::installsList() const
{
    return m_installsList;
}

QString QmakeParserProFileNode::sourceDir() const
{
    return m_projectDir;
}

QString QmakeParserProFileNode::buildDir(QmakeBuildConfiguration *bc) const
{
    const QDir srcDirRoot = m_project->rootProjectNode()->sourceDir();
    const QString relativeDir = srcDirRoot.relativeFilePath(m_projectDir);
    if (!bc && m_project->activeTarget())
        bc = static_cast<QmakeBuildConfiguration *>(m_project->activeTarget()->activeBuildConfiguration());
    if (!bc)
        return QString();
    return QDir::cleanPath(QDir(bc->buildDirectory().toString()).absoluteFilePath(relativeDir));
}

QStringList QmakeParserProFileNode::generatedFiles(const QString &buildDir,
                                             const ProjectExplorer::FileNode *sourceFile) const
{
    // The mechanism for finding the file names is rather crude, but as we
    // cannot parse QMAKE_EXTRA_COMPILERS and qmake has facilities to put
    // ui_*.h files into a special directory, or even change the .h suffix, we
    // cannot help doing this here.
    switch (sourceFile->fileType()) {
    case FileType::Form: {
        FileName location;
        auto it = m_varValues.constFind(Variable::UiDir);
        if (it != m_varValues.constEnd() && !it.value().isEmpty())
            location = FileName::fromString(it.value().front());
        else
            location = FileName::fromString(buildDir);
        if (location.isEmpty())
            return QStringList();
        location.appendPath(QLatin1String("ui_")
                            + sourceFile->filePath().toFileInfo().completeBaseName()
                            + singleVariableValue(Variable::HeaderExtension));
        return QStringList(QDir::cleanPath(location.toString()));
    }
    case FileType::StateChart: {
        if (buildDir.isEmpty())
            return QStringList();
        QString location = QDir::cleanPath(FileName::fromString(buildDir).appendPath(
                    sourceFile->filePath().toFileInfo().completeBaseName()).toString());
        return QStringList({location + singleVariableValue(Variable::HeaderExtension),
                            location + singleVariableValue(Variable::CppExtension)});
    }
    default:
        // TODO: Other types will be added when adapters for their compilers become available.
        return QStringList();
    }
}

QList<ExtraCompiler *> QmakeParserProFileNode::extraCompilers() const
{
    return m_extraCompilers;
}

void QmakeParserProFileNode::updateGeneratedFiles(const QString &buildDir)
{
    // We can do this because other plugins are not supposed to keep the compilers around.
    qDeleteAll(m_extraCompilers);
    m_extraCompilers.clear();

    // Only those project types can have generated files for us
    if (m_projectType != ProjectType::ApplicationTemplate
            && m_projectType != ProjectType::SharedLibraryTemplate
            && m_projectType != ProjectType::StaticLibraryTemplate) {
        return;
    }

    QList<ExtraCompilerFactory *> factories =
            ProjectExplorer::ExtraCompilerFactory::extraCompilerFactories();

    FindGeneratorSourcesVisitor filesVisitor(factories, [&](
                                             FileNode *file, ExtraCompilerFactory *factory) {
        QStringList generated = generatedFiles(buildDir, file);
        if (!generated.isEmpty()) {
            FileNameList fileNames = Utils::transform(generated, [](const QString &name) {
                return FileName::fromString(name);
            });
            m_extraCompilers.append(factory->create(m_project, file->filePath(), fileNames));
        }
    });

    // Find all generated files
    accept(&filesVisitor);
}