#include "Gui/Controllers/SceneController.h"

#include "Engine/Network/NetworkManager.h"
#include "Engine/Serializer/Serializer.h"

#include <QFileInfo>
#include <QSettings>

namespace {

// How many files the recent list keeps.
constexpr int maxRecentFiles = 10;

} // namespace

helix::ui::SceneController::SceneController(QObject* parent) : QObject(parent) {}

QString helix::ui::SceneController::currentFileName() const
{
    if (currentFilePath_.isEmpty()) return {};
    return QFileInfo(currentFilePath_).fileName();
}

bool helix::ui::SceneController::hasCurrentFile() const { return !currentFilePath_.isEmpty(); }

QStringList helix::ui::SceneController::recentFiles() const
{
    return QSettings().value("recentFiles").toStringList();
}

void helix::ui::SceneController::newScene()
{
    helix::nt::nm().clear();
    currentFilePath_.clear();
    Q_EMIT currentFileChanged();
}

void helix::ui::SceneController::open(const QUrl& fileUrl) { openPath(fileUrl.toLocalFile()); }

void helix::ui::SceneController::openPath(const QString& filePath)
{
    if (filePath.isEmpty()) return;
    readFrom(filePath);
}

void helix::ui::SceneController::save()
{
    if (currentFilePath_.isEmpty()) return;
    writeTo(currentFilePath_);
}

void helix::ui::SceneController::saveAs(const QUrl& fileUrl)
{
    const QString filePath = fileUrl.toLocalFile();
    if (filePath.isEmpty()) return;
    writeTo(filePath);
}

void helix::ui::SceneController::writeTo(const QString& filePath)
{
    helix::nt::Serializer().save(helix::nt::nm(), filePath.toStdString());
    currentFilePath_ = filePath;
    Q_EMIT currentFileChanged();
    rememberRecent(filePath);
}

void helix::ui::SceneController::readFrom(const QString& filePath)
{
    helix::nt::Serializer().load(helix::nt::nm(), filePath.toStdString());
    currentFilePath_ = filePath;
    Q_EMIT currentFileChanged();
    rememberRecent(filePath);
}

void helix::ui::SceneController::rememberRecent(const QString& filePath)
{
    QSettings settings;
    QStringList recent = settings.value("recentFiles").toStringList();
    recent.removeAll(filePath);
    recent.prepend(filePath);
    settings.setValue("recentFiles", recent.mid(0, maxRecentFiles));
    Q_EMIT recentFilesChanged();
}
