#pragma once

#include <QObject>
#include <QFileSystemWatcher>
#include <QHash>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class ProjectModel final : public QObject
{
    Q_OBJECT

public:
    explicit ProjectModel(QObject *parent = nullptr);

    void setProjectDirectory(const QString &path);
    QString projectDirectory() const;

    QStringList files() const;

    QVariantList projectTree() const;

    // Resolve a file query against the indexed project.
    //
    // Returns:
    // {
    //   "status": "resolved" | "ambiguous" | "not_found" | "invalid",
    //   "query": "...",
    //   "path": "...",               // when resolved
    //   "candidates": [...]          // when ambiguous
    // }
    Q_INVOKABLE QVariantMap resolveFile(
        const QString &query,
        const QString &relativeTo = QString()
    ) const;

    Q_INVOKABLE QString readFile(
        const QString &relativePath
    ) const;

    Q_INVOKABLE QString readFileRange(
        const QString &relativePath,
        int startLine,
        int endLine
    ) const;

    Q_INVOKABLE QVariantList search(
        const QString &query,
        int maxResults = 50
    ) const;

    void setEditorBuffer(
        const QString &relativePath,
        const QString &code
    );

    void clearEditorBuffer(
        const QString &relativePath
    );

signals:
    void projectChanged();
    void filesChanged(const QStringList &files);
    void projectTreeChanged(const QVariantList &tree);
    void fileChanged(const QString &relativePath);

private slots:
    void onFileSystemChanged(const QString &path);

private:
    void scanProject();

    void scanDirectory(
        const QString &absoluteDirectory,
        const QString &relativeDirectory
    );

    bool shouldSkipDirectory(
        const QString &directoryName
    ) const;

    bool isLikelyTextFile(
        const QString &path
    ) const;

    QString resolvePath(
        const QString &relativePath
    ) const;

    QString normalizeRelativePath(
        const QString &relativePath
    ) const;

    QString normalizeAbsolutePath(
        const QString &path
    ) const;

    QString readDiskFile(
        const QString &absolutePath
    ) const;

    QString readEditorOrDiskFile(
        const QString &relativePath
    ) const;

    bool isPathInsideProject(
        const QString &absolutePath
    ) const;

    QString relativePathFromAbsolute(
        const QString &absolutePath
    ) const;

    void updateWatcher();

    QVariantList buildProjectTree() const;

    void insertTreePath(
        QVariantList &nodes,
        const QStringList &parts,
        const QString &fullPath
    ) const;

    int countSharedSuffixComponents(
        const QStringList &a,
        const QStringList &b
    ) const;

private:
    QString m_projectDirectory;
    QStringList m_files;

    QFileSystemWatcher m_watcher;

    // Unsaved editor buffers override disk contents.
    QHash<QString, QString> m_editorBuffers;
};