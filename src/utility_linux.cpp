#include "utility.h"
#include "report.h"
#include <QDesktopServices>
#include <QProcess>
#include <QStandardPaths>
#include <QTextCodec>

bool MOBase::removeDir(const QString& dirName) {
    QDir dir(dirName);

    if (dir.exists()) {
        for(const QFileInfo& info : dir.entryInfoList(QDir::NoDotAndDotDot | QDir::System | QDir::Hidden  | QDir::AllDirs | QDir::Files, QDir::DirsFirst)) {
                if (info.isDir()) {
                    if (!removeDir(info.absoluteFilePath())) {
                        return false;
                    }
                } else {
                    QFile file(info.absoluteFilePath());
                    if (!file.remove()) {
                        reportError(QObject::tr("removal of \"%1\" failed: %2").arg(info.absoluteFilePath()).arg(file.errorString()));
                        return false;
                    }
                }
            }

        if (!dir.rmdir(dirName)) {
            reportError(QObject::tr("removal of \"%1\" failed").arg(dir.absolutePath()));
            return false;
        }
    } else {
        reportError(QObject::tr("\"%1\" doesn't exist (remove)").arg(dirName));
        return false;
    }

    return true;
}

bool MOBase::copyDir(const QString &sourceName, const QString &destinationName, bool merge) {
    QDir sourceDir(sourceName);
    if(!sourceDir.exists()) {
        return false;
    }
    QDir destDir(destinationName);
    if(!destDir.exists()) {
        destDir.mkdir(destinationName);
    } else if(!merge) {
        return false;
    }

    QStringList files = sourceDir.entryList(QDir::Files);
    for(const QString& fileName : qAsConst(files)) {
        QString srcName = sourceName + "/" + fileName;
        QString destName = destinationName + "/" + fileName;
        QFile::copy(srcName, destName);
    }

    files.clear();
    // we leave out symlinks because that could cause an endless recursion
    QStringList subDirs = sourceDir.entryList(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for(const QString& subDir : qAsConst(subDirs)) {
        QString srcName = sourceName + "/" + subDir;
        QString destName = destinationName + "/" + subDir;
        copyDir(srcName, destName, merge);
    }
    return true;
}

bool MOBase::moveFileRecursive(const QString &source, const QString &baseDir, const QString &destination)
{
    QStringList pathComponents = destination.split("/");
    QString path = baseDir;
    for (QStringList::Iterator iter = pathComponents.begin(); iter != pathComponents.end() - 1; ++iter) {
        path.append("/").append(*iter);
        if (!QDir(path).exists() && !QDir().mkdir(path)) {
            reportError(QObject::tr("failed to create directory \"%1\"").arg(path));
            return false;
        }
    }

    QString destinationAbsolute = baseDir.mid(0).append("/").append(destination);
    if (!QFile::rename(source, destinationAbsolute)) {
        // move failed, try copy & delete
        if (!QFile::copy(source, destinationAbsolute)) {
            reportError(QObject::tr("failed to copy \"%1\" to \"%2\"").arg(source).arg(destinationAbsolute));
            return false;
        } else {
            QFile::remove(source);
        }
    }
    return true;
}

bool MOBase::copyFileRecursive(const QString &source, const QString &baseDir, const QString &destination)
{
    QStringList pathComponents = destination.split("/");
    QString path = baseDir;
    for (QStringList::Iterator iter = pathComponents.begin(); iter != pathComponents.end() - 1; ++iter) {
        path.append("/").append(*iter);
        if (!QDir(path).exists() && !QDir().mkdir(path)) {
            reportError(QObject::tr("failed to create directory \"%1\"").arg(path));
            return false;
        }
    }

    QString destinationAbsolute = baseDir.mid(0).append("/").append(destination);
    if (!QFile::copy(source, destinationAbsolute)) {
        reportError(QObject::tr("failed to copy \"%1\" to \"%2\"").arg(source).arg(destinationAbsolute));
        return false;
    }
    return true;
}

bool MOBase::shell::ExploreFile(const QFileInfo& info)
{
    return QDesktopServices::openUrl(QUrl::fromLocalFile(info.isDir() ? info.path() : info.dir().path()));
}

bool MOBase::shell::ExploreFile(const QString& path)
{
    return ExploreFile(QFileInfo(path));
}

bool MOBase::shell::ExploreFile(const QDir& dir)
{
    return QDesktopServices::openUrl(QUrl::fromLocalFile(dir.path()));
}

bool MOBase::shell::OpenFile(const QString& path)
{
    return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

bool MOBase::shell::OpenLink(const QUrl& url)
{
    return QDesktopServices::openUrl(url);
}

bool MOBase::shell::Execute(const QString& program, const QStringList& params)
{
    return QProcess::startDetached(program, params);
}

std::wstring MOBase::ToWString(const QString& source)
{
    return source.toStdWString();
}

std::string MOBase::ToString(const QString& source, bool utf8)
{
    return (utf8 ? source.toUtf8() : source.toLocal8Bit()).toStdString();
}

QString MOBase::ToQString(const std::string& source)
{
    return QString::fromStdString(source);
}

QString MOBase::ToQString(const std::wstring& source)
{
    return QString::fromStdWString(source);
}

QString MOBase::getDesktopDirectory()
{
    auto dirs = QStandardPaths::standardLocations(QStandardPaths::DesktopLocation);
    return dirs.empty() ? "" : dirs.first();
}

QString MOBase::getStartMenuDirectory()
{
    auto dirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    return dirs.empty() ? "" : dirs.first();
}

bool MOBase::fixDirectoryName(QString& name)
{
    // we probably don't need to do anything here
    return true;
}

QString MOBase::readFileText(const QString& fileName, QString* encoding)
{
    // the functions from QTextCodec we use are supposed to be reentrant so it's
    // safe to use statics for that
    static QTextCodec *utf8Codec = QTextCodec::codecForName("utf-8");

    QFile textFile(fileName);
    if (!textFile.open(QIODevice::ReadOnly)) {
        return QString();
    }

    QByteArray buffer = textFile.readAll();
    QTextCodec *codec = QTextCodec::codecForUtfText(buffer, utf8Codec);
    QString text = codec->toUnicode(buffer);

    // check reverse conversion. If this was unicode text there can't be data loss
    // this assumes QString doesn't normalize the data in any way so this is a bit unsafe
    if (codec->fromUnicode(text) != buffer) {
        qDebug("conversion failed assuming local encoding");
        codec = QTextCodec::codecForLocale();
        text = codec->toUnicode(buffer);
    }

    if (encoding != nullptr) {
        *encoding = codec->name();
    }

    return text;
}

void MOBase::removeOldFiles(const QString &path, const QString &pattern, int numToKeep, QDir::SortFlags sorting)
{
    QFileInfoList files = QDir(path).entryInfoList(QStringList(pattern), QDir::Files, sorting);

    if (files.count() > numToKeep) {
        QStringList deleteFiles;
        for (int i = 0; i < files.count() - numToKeep; ++i) {
            deleteFiles.append(files.at(i).absoluteFilePath());
        }

        if (!shellDelete(deleteFiles)) {
            qWarning("failed to remove old files");
        }
    }
}

QIcon MOBase::iconForExecutable(const QString &filePath)
{
    // TODO handle .exe icons
    return QIcon(":/MO/gui/executable");
}

std::wstring MOBase::formatSystemMessage(std::errc id)
{
    return formatSystemMessageQ(id).toStdWString();
}

QString MOBase::formatSystemMessageQ(std::errc id)
{
    return QString::fromStdString(std::make_error_code(id).message());
}