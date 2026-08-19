#include "ProjectModel.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <limits>

namespace
{
constexpr qsizetype kMaxFileSize = 4 * 1024 * 1024;

const QSet<QString> kSkippedDirectories = {
    QStringLiteral(".git"),
    QStringLiteral(".svn"),
    QStringLiteral(".hg"),
    QStringLiteral("build"),
    QStringLiteral("build-debug"),
    QStringLiteral("build-release"),
    QStringLiteral("cmake-build-debug"),
    QStringLiteral("cmake-build-release"),
    QStringLiteral("node_modules"),
    QStringLiteral("dist"),
    QStringLiteral("target"),
    QStringLiteral(".cache"),
    QStringLiteral(".idea"),
    QStringLiteral(".vscode")
};

const QSet<QString> kTextExtensions = {
    QStringLiteral("c"),
    QStringLiteral("cc"),
    QStringLiteral("cpp"),
    QStringLiteral("cxx"),
    QStringLiteral("h"),
    QStringLiteral("hh"),
    QStringLiteral("hpp"),
    QStringLiteral("hxx"),

    QStringLiteral("js"),
    QStringLiteral("jsx"),
    QStringLiteral("ts"),
    QStringLiteral("tsx"),

    QStringLiteral("py"),
    QStringLiteral("rb"),
    QStringLiteral("go"),
    QStringLiteral("rs"),
    QStringLiteral("java"),
    QStringLiteral("kt"),
    QStringLiteral("swift"),
    QStringLiteral("cs"),

    QStringLiteral("html"),
    QStringLiteral("htm"),
    QStringLiteral("css"),
    QStringLiteral("scss"),
    QStringLiteral("less"),

    QStringLiteral("json"),
    QStringLiteral("yaml"),
    QStringLiteral("yml"),
    QStringLiteral("toml"),
    QStringLiteral("xml"),

    QStringLiteral("cmake"),
    QStringLiteral("pro"),
    QStringLiteral("pri"),

    QStringLiteral("md"),
    QStringLiteral("txt"),
    QStringLiteral("sh"),
    QStringLiteral("bat"),
    QStringLiteral("ps1"),

    QStringLiteral("sql"),
    QStringLiteral("graphql")
};

QString cleanSnippet(const QString &line)
{
    QString result = line;

    result.replace(
        '\t',
        QStringLiteral("    ")
    );

    constexpr int kMaxSnippetLength = 300;

    if (result.length() > kMaxSnippetLength) {
        result = result.left(kMaxSnippetLength);
        result += QStringLiteral("...");
    }

    return result;
}

QStringList pathComponents(
    const QString &path
)
{
    return path.split(
        '/',
        Qt::SkipEmptyParts
    );
}

bool pathEquals(
    const QString &a,
    const QString &b
)
{
    return a.compare(
        b,
        Qt::CaseInsensitive
    ) == 0;
}
}

ProjectModel::ProjectModel(
    QObject *parent
)
    : QObject(parent)
{
    connect(
        &m_watcher,
        &QFileSystemWatcher::fileChanged,
        this,
        &ProjectModel::onFileSystemChanged
    );

    connect(
        &m_watcher,
        &QFileSystemWatcher::directoryChanged,
        this,
        &ProjectModel::onFileSystemChanged
    );
}

void ProjectModel::setProjectDirectory(
    const QString &path
)
{
    QFileInfo info(path);

    if (!info.exists() || !info.isDir()) {
        qWarning()
            << "[ProjectModel] Invalid project directory:"
            << path;

        return;
    }

    const QString canonicalPath =
        info.canonicalFilePath();

    if (canonicalPath.isEmpty()) {
        qWarning()
            << "[ProjectModel] Could not canonicalize:"
            << path;

        return;
    }

    if (
        canonicalPath ==
        m_projectDirectory
    ) {
        scanProject();
        return;
    }

    m_projectDirectory =
        canonicalPath;

    m_editorBuffers.clear();

    scanProject();

    emit projectChanged();
}

QString ProjectModel::projectDirectory() const
{
    return m_projectDirectory;
}

QStringList ProjectModel::files() const
{
    return m_files;
}

QVariantList ProjectModel::projectTree() const
{
    return buildProjectTree();
}

QString ProjectModel::normalizeRelativePath(
    const QString &relativePath
) const
{
    QString normalized =
        QDir::fromNativeSeparators(
            relativePath.trimmed()
        );

    while (
        normalized.startsWith(
            QStringLiteral("./")
        )
    ) {
        normalized.remove(0, 2);
    }

    normalized =
        QDir::cleanPath(
            normalized
        );

    if (
        normalized == QStringLiteral(".")
    ) {
        normalized.clear();
    }

    return normalized;
}

QString ProjectModel::normalizeAbsolutePath(
    const QString &path
) const
{
    const QFileInfo info(path);

    if (!info.exists()) {
        return {};
    }

    return info.canonicalFilePath();
}

bool ProjectModel::isPathInsideProject(
    const QString &absolutePath
) const
{
    if (m_projectDirectory.isEmpty()) {
        return false;
    }

    const QString projectRoot =
        QFileInfo(
            m_projectDirectory
        ).canonicalFilePath();

    const QString candidate =
        QFileInfo(
            absolutePath
        ).canonicalFilePath();

    if (
        projectRoot.isEmpty()
        || candidate.isEmpty()
    ) {
        return false;
    }

    if (candidate == projectRoot) {
        return true;
    }

    const QString prefix =
        projectRoot +
        QDir::separator();

    return candidate.startsWith(
        prefix
    );
}

QString ProjectModel::relativePathFromAbsolute(
    const QString &absolutePath
) const
{
    if (
        !isPathInsideProject(
            absolutePath
        )
    ) {
        return {};
    }

    return QDir(
        m_projectDirectory
    ).relativeFilePath(
        absolutePath
    );
}

int ProjectModel::countSharedSuffixComponents(
    const QStringList &a,
    const QStringList &b
) const
{
    int count = 0;

    int i = a.size() - 1;
    int j = b.size() - 1;

    while (
        i >= 0
        && j >= 0
    ) {
        if (
            !pathEquals(
                a.at(i),
                b.at(j)
            )
        ) {
            break;
        }

        ++count;
        --i;
        --j;
    }

    return count;
}

QVariantMap ProjectModel::resolveFile(
    const QString &query,
    const QString &relativeTo
) const
{
    QVariantMap result;

    result.insert(
        QStringLiteral("query"),
        query
    );

    const QString trimmed =
        query.trimmed();

    if (trimmed.isEmpty()) {
        result.insert(
            QStringLiteral("status"),
            QStringLiteral("invalid")
        );

        result.insert(
            QStringLiteral("message"),
            QStringLiteral(
                "File query is empty."
            )
        );

        return result;
    }

    if (m_projectDirectory.isEmpty()) {
        result.insert(
            QStringLiteral("status"),
            QStringLiteral("invalid")
        );

        result.insert(
            QStringLiteral("message"),
            QStringLiteral(
                "No project directory is set."
            )
        );

        return result;
    }

    const QString normalizedQuery =
        QDir::fromNativeSeparators(
            trimmed
        );

    // --------------------------------------------------------
    // Absolute path
    // --------------------------------------------------------

    QFileInfo absoluteInfo(
        normalizedQuery
    );

    if (absoluteInfo.isAbsolute()) {
        const QString absolute =
            absoluteInfo.canonicalFilePath();

        if (
            !absolute.isEmpty()
            && isPathInsideProject(
                absolute
            )
        ) {
            const QString relative =
                relativePathFromAbsolute(
                    absolute
                );

            const int index =
                m_files.indexOf(relative);

            if (index >= 0) {
                result.insert(
                    QStringLiteral("status"),
                    QStringLiteral("resolved")
                );

                result.insert(
                    QStringLiteral("path"),
                    relative
                );

                result.insert(
                    QStringLiteral("matchedBy"),
                    QStringLiteral("absolute_path")
                );

                return result;
            }
        }
    }

    // --------------------------------------------------------
    // Build relative-to-file candidate
    // --------------------------------------------------------

    QString relativeBase =
        normalizeRelativePath(
            relativeTo
        );

    if (
        !relativeBase.isEmpty()
        && !m_files.contains(
            relativeBase
        )
    ) {
        const QStringList matches =
            m_files.filter(
                QRegularExpression(
                    QRegularExpression::escape(
                        relativeBase
                    ),
                    QRegularExpression::CaseInsensitiveOption
                )
            );

        if (!matches.isEmpty()) {
            relativeBase =
                matches.first();
        }
    }

    QString relativeDirectory;

    if (!relativeBase.isEmpty()) {
        relativeDirectory =
            QFileInfo(
                relativeBase
            ).path();

        if (
            relativeDirectory == QStringLiteral(".")
        ) {
            relativeDirectory.clear();
        }
    }

    struct Candidate
    {
        QString path;
        int score = 0;
        QString matchedBy;
    };

    QList<Candidate> candidates;

    auto addCandidate =
        [&candidates](
            const QString &path,
            int score,
            const QString &matchedBy
        ) {
            for (Candidate &existing :
                 candidates) {
                if (
                    existing.path == path
                ) {
                    if (
                        score >
                        existing.score
                    ) {
                        existing.score =
                            score;

                        existing.matchedBy =
                            matchedBy;
                    }

                    return;
                }
            }

            candidates.append(
                Candidate{
                    path,
                    score,
                    matchedBy
                }
            );
        };

    const QString normalized =
        normalizeRelativePath(
            normalizedQuery
        );

    const bool queryLooksRelative =
        normalized.startsWith(
            QStringLiteral("../")
        )
        || normalized.startsWith(
            QStringLiteral("./")
        );

    // --------------------------------------------------------
    // 1. Exact project-relative path
    // --------------------------------------------------------

    for (
        const QString &file :
        m_files
    ) {
        if (file == normalized) {
            addCandidate(
                file,
                10000,
                QStringLiteral(
                    "exact_relative_path"
                )
            );
        }
    }

    // --------------------------------------------------------
    // 2. Case-insensitive exact project-relative path
    // --------------------------------------------------------

    for (
        const QString &file :
        m_files
    ) {
        if (
            file.compare(
                normalized,
                Qt::CaseInsensitive
            ) == 0
        ) {
            addCandidate(
                file,
                9500,
                QStringLiteral(
                    "case_insensitive_relative_path"
                )
            );
        }
    }

    // --------------------------------------------------------
    // 3. Resolve query relative to current file
    // --------------------------------------------------------

    if (!relativeDirectory.isEmpty()) {
        const QString candidatePath =
            normalizeRelativePath(
                relativeDirectory +
                '/' +
                normalized
            );

        if (
            !candidatePath.isEmpty()
            && !candidatePath.startsWith(
                QStringLiteral("../")
            )
        ) {
            for (
                const QString &file :
                m_files
            ) {
                if (file == candidatePath) {
                    addCandidate(
                        file,
                        queryLooksRelative
                            ? 9200
                            : 8500,
                        QStringLiteral(
                            "relative_to_current_file"
                        )
                    );
                } else if (
                    file.compare(
                        candidatePath,
                        Qt::CaseInsensitive
                    ) == 0
                ) {
                    addCandidate(
                        file,
                        queryLooksRelative
                            ? 9100
                            : 8400,
                        QStringLiteral(
                            "case_insensitive_relative_to_current_file"
                        )
                    );
                }
            }
        }
    }

    // --------------------------------------------------------
    // 4. Query as a suffix of project paths
    //
    // Example:
    //   rendering/Camera.h
    //
    // matches:
    //   include/rendering/Camera.h
    // --------------------------------------------------------

    const QStringList queryParts =
        pathComponents(
            normalized
        );

    if (!queryParts.isEmpty()) {
        for (
            const QString &file :
            m_files
        ) {
            const QStringList fileParts =
                pathComponents(file);

            const int shared =
                countSharedSuffixComponents(
                    fileParts,
                    queryParts
                );

            if (
                shared ==
                queryParts.size()
            ) {
                const int score =
                    5000 +
                    shared * 200;

                addCandidate(
                    file,
                    score,
                    QStringLiteral(
                        "path_suffix"
                    )
                );
            }
        }
    }

    // --------------------------------------------------------
    // 5. Same directory + basename
    // --------------------------------------------------------

    const QString basename =
        QFileInfo(
            normalized
        ).fileName();

    if (!basename.isEmpty()) {
        for (
            const QString &file :
            m_files
        ) {
            const QString fileName =
                QFileInfo(
                    file
                ).fileName();

            if (
                fileName.compare(
                    basename,
                    Qt::CaseInsensitive
                ) != 0
            ) {
                continue;
            }

            int score = 3000;

            QString matchedBy =
                QStringLiteral(
                    "basename"
                );

            const QString fileDirectory =
                QFileInfo(
                    file
                ).path();

            if (
                !relativeDirectory.isEmpty()
                && fileDirectory.compare(
                    relativeDirectory,
                    Qt::CaseInsensitive
                ) == 0
            ) {
                score =
                    8000;

                matchedBy =
                    QStringLiteral(
                        "same_directory_basename"
                    );
            } else if (
                !relativeDirectory.isEmpty()
            ) {
                const int sharedDirectories =
                    countSharedSuffixComponents(
                        pathComponents(
                            fileDirectory
                        ),
                        pathComponents(
                            relativeDirectory
                        )
                    );

                score +=
                    sharedDirectories * 250;

                if (
                    sharedDirectories > 0
                ) {
                    matchedBy =
                        QStringLiteral(
                            "basename_shared_directory"
                        );
                }
            }

            addCandidate(
                file,
                score,
                matchedBy
            );
        }
    }

    if (candidates.isEmpty()) {
        result.insert(
            QStringLiteral("status"),
            QStringLiteral("not_found")
        );

        result.insert(
            QStringLiteral("message"),
            QStringLiteral(
                "No project file matched '%1'."
            ).arg(
                query
            )
        );

        return result;
    }

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const Candidate &a, const Candidate &b) {
            if (a.score != b.score) {
                return a.score > b.score;
            }

            return a.path.compare(
                b.path,
                Qt::CaseInsensitive
            ) < 0;
        }
    );

    const int bestScore =
        candidates.first().score;

    QVariantList candidatePaths;

    for (
        const Candidate &candidate :
        candidates
    ) {
        if (
            candidate.score ==
            bestScore
        ) {
            candidatePaths.append(
                candidate.path
            );
        }
    }

    if (
        candidatePaths.size() == 1
    ) {
        result.insert(
            QStringLiteral("status"),
            QStringLiteral("resolved")
        );

        result.insert(
            QStringLiteral("path"),
            candidatePaths.first()
        );

        result.insert(
            QStringLiteral("matchedBy"),
            candidates.first().matchedBy
        );

        return result;
    }

    result.insert(
        QStringLiteral("status"),
        QStringLiteral("ambiguous")
    );

    result.insert(
        QStringLiteral("candidates"),
        candidatePaths
    );

    result.insert(
        QStringLiteral("message"),
        QStringLiteral(
            "The file query '%1' is ambiguous."
        ).arg(
            query
        )
    );

    return result;
}

QString ProjectModel::resolvePath(
    const QString &relativePath
) const
{
    if (m_projectDirectory.isEmpty()) {
        return {};
    }

    const QString normalized =
        normalizeRelativePath(
            relativePath
        );

    if (
        normalized.isEmpty()
        || normalized == QStringLiteral("..")
        || normalized.startsWith(
            QStringLiteral("../")
        )
    ) {
        return {};
    }

    const QString absolutePath =
        QDir(
            m_projectDirectory
        ).absoluteFilePath(
            normalized
        );

    QFileInfo candidateInfo(
        absolutePath
    );

    if (
        !candidateInfo.exists()
        || !candidateInfo.isFile()
    ) {
        return {};
    }

    const QString canonical =
        candidateInfo.canonicalFilePath();

    if (
        canonical.isEmpty()
        || !isPathInsideProject(
            canonical
        )
    ) {
        return {};
    }

    return canonical;
}

QString ProjectModel::readDiskFile(
    const QString &absolutePath
) const
{
    const QFileInfo info(
        absolutePath
    );

    if (
        !info.exists()
        || !info.isFile()
        || info.size() > kMaxFileSize
    ) {
        return {};
    }

    QFile file(
        absolutePath
    );

    if (!file.open(
            QIODevice::ReadOnly
        )) {
        return {};
    }

    const QByteArray data =
        file.readAll();

    if (data.contains('\0')) {
        return {};
    }

    return QString::fromUtf8(
        data
    );
}

QString ProjectModel::readEditorOrDiskFile(
    const QString &relativePath
) const
{
    const QString normalized =
        normalizeRelativePath(
            relativePath
        );

    const auto it =
        m_editorBuffers.constFind(
            normalized
        );

    if (
        it != m_editorBuffers.constEnd()
    ) {
        return it.value();
    }

    const QString absolute =
        resolvePath(
            normalized
        );

    if (absolute.isEmpty()) {
        return {};
    }

    return readDiskFile(
        absolute
    );
}

QString ProjectModel::readFile(
    const QString &relativePath
) const
{
    // Exact project-relative reads remain cheap and deterministic.
    const QString normalized =
        normalizeRelativePath(
            relativePath
        );

    if (
        m_files.contains(
            normalized
        )
    ) {
        return readEditorOrDiskFile(
            normalized
        );
    }

    // If the requested path isn't exact, resolve it as a project
    // file query. This allows:
    //
    // Camera.h
    // rendering/Camera.h
    // ../../include/rendering/Camera.h
    //
    // without requiring the model to understand your include paths.
    const QVariantMap resolution =
        resolveFile(
            relativePath
        );

    const QString status =
        resolution.value(
            QStringLiteral("status")
        ).toString();

    if (
        status ==
        QStringLiteral("resolved")
    ) {
        const QString resolvedPath =
            resolution.value(
                QStringLiteral("path")
            ).toString();

        return readEditorOrDiskFile(
            resolvedPath
        );
    }

    return {};
}

QString ProjectModel::readFileRange(
    const QString &relativePath,
    int startLine,
    int endLine
) const
{
    const QVariantMap resolution =
        resolveFile(
            relativePath
        );

    const QString status =
        resolution.value(
            QStringLiteral("status")
        ).toString();

    if (
        status !=
        QStringLiteral("resolved")
    ) {
        return {};
    }

    const QString resolvedPath =
        resolution.value(
            QStringLiteral("path")
        ).toString();

    const QString content =
        readEditorOrDiskFile(
            resolvedPath
        );

    if (content.isEmpty()) {
        return {};
    }

    startLine =
        qMax(
            1,
            startLine
        );

    endLine =
        qMax(
            startLine,
            endLine
        );

    const QStringList lines =
        content.split(
            '\n',
            Qt::KeepEmptyParts
        );

    if (
        startLine >
        lines.size()
    ) {
        return {};
    }

    endLine =
        qMin(
            endLine,
            lines.size()
        );

    QStringList selected;

    for (
        int i = startLine - 1;
        i < endLine;
        ++i
    ) {
        selected.append(
            lines.at(i)
        );
    }

    return selected.join(
        '\n'
    );
}

QVariantList ProjectModel::search(
    const QString &query,
    int maxResults
) const
{
    QVariantList results;

    const QString searchText =
        query.trimmed();

    if (searchText.isEmpty()) {
        return results;
    }

    maxResults =
        qBound(
            1,
            maxResults,
            500
        );

    for (
        const QString &relativePath :
        m_files
    ) {
        if (
            results.size() >=
            maxResults
        ) {
            break;
        }

        const QString content =
            readEditorOrDiskFile(
                relativePath
            );

        if (content.isEmpty()) {
            continue;
        }

        const QStringList lines =
            content.split(
                '\n',
                Qt::KeepEmptyParts
            );

        for (
            int i = 0;
            i < lines.size();
            ++i
        ) {
            if (
                results.size() >=
                maxResults
            ) {
                break;
            }

            const QString &line =
                lines.at(i);

            if (
                !line.contains(
                    searchText,
                    Qt::CaseInsensitive
                )
            ) {
                continue;
            }

            QVariantMap result;

            result.insert(
                QStringLiteral("file"),
                relativePath
            );

            result.insert(
                QStringLiteral("line"),
                i + 1
            );

            result.insert(
                QStringLiteral("text"),
                cleanSnippet(line)
            );

            results.append(
                result
            );
        }
    }

    return results;
}

QVariantList ProjectModel::buildProjectTree() const
{
    QVariantList roots;

    for (
        const QString &file :
        m_files
    ) {
        const QStringList parts =
            file.split(
                '/',
                Qt::SkipEmptyParts
            );

        if (parts.isEmpty()) {
            continue;
        }

        insertTreePath(
            roots,
            parts,
            file
        );
    }

    return roots;
}

void ProjectModel::insertTreePath(
    QVariantList &nodes,
    const QStringList &parts,
    const QString &fullPath
) const
{
    if (parts.isEmpty()) {
        return;
    }

    const QString name =
        parts.first();

    const bool isFile =
        parts.size() == 1;

    int foundIndex = -1;

    for (
        int i = 0;
        i < nodes.size();
        ++i
    ) {
        const QVariantMap item =
            nodes.at(i).toMap();

        if (
            item.value(
                QStringLiteral("name")
            ).toString() ==
            name
        ) {
            foundIndex = i;
            break;
        }
    }

    QVariantMap node;

    if (foundIndex >= 0) {
        node =
            nodes.at(
                foundIndex
            ).toMap();
    } else {
        node.insert(
            QStringLiteral("name"),
            name
        );

        node.insert(
            QStringLiteral("type"),
            isFile
                ? QStringLiteral("file")
                : QStringLiteral("directory")
        );

        if (isFile) {
            node.insert(
                QStringLiteral("path"),
                fullPath
            );
        } else {
            node.insert(
                QStringLiteral("children"),
                QVariantList()
            );
        }
    }

    if (!isFile) {
        QVariantList children =
            node.value(
                QStringLiteral("children")
            ).toList();

        insertTreePath(
            children,
            parts.mid(1),
            fullPath
        );

        node.insert(
            QStringLiteral("children"),
            children
        );
    }

    if (foundIndex >= 0) {
        nodes[foundIndex] =
            node;
    } else {
        nodes.append(
            node
        );
    }
}

void ProjectModel::scanProject()
{
    m_files.clear();

    if (
        m_projectDirectory.isEmpty()
    ) {
        updateWatcher();

        emit filesChanged(
            m_files
        );

        emit projectTreeChanged(
            QVariantList()
        );

        return;
    }

    scanDirectory(
        m_projectDirectory,
        QString()
    );

    m_files.sort(
        Qt::CaseInsensitive
    );

    updateWatcher();

    const QVariantList tree =
        buildProjectTree();

    emit filesChanged(
        m_files
    );

    emit projectTreeChanged(
        tree
    );

    qDebug()
        << "[ProjectModel] Project scanned:"
        << m_projectDirectory
        << "files:"
        << m_files.size();
}

void ProjectModel::scanDirectory(
    const QString &absoluteDirectory,
    const QString &relativeDirectory
)
{
    QDir directory(
        absoluteDirectory
    );

    const QFileInfoList entries =
        directory.entryInfoList(
            QDir::NoDotAndDotDot |
            QDir::AllEntries,
            QDir::DirsFirst |
            QDir::Name
        );

    for (
        const QFileInfo &entry :
        entries
    ) {
        if (entry.isDir()) {
            if (
                shouldSkipDirectory(
                    entry.fileName()
                )
            ) {
                continue;
            }

            QString childRelative =
                relativeDirectory;

            if (
                !childRelative.isEmpty()
            ) {
                childRelative += '/';
            }

            childRelative +=
                entry.fileName();

            scanDirectory(
                entry.absoluteFilePath(),
                childRelative
            );

            continue;
        }

        if (!entry.isFile()) {
            continue;
        }

        if (
            !isLikelyTextFile(
                entry.absoluteFilePath()
            )
        ) {
            continue;
        }

        QString relativePath =
            relativeDirectory;

        if (
            !relativePath.isEmpty()
        ) {
            relativePath += '/';
        }

        relativePath +=
            entry.fileName();

        m_files.append(
            QDir::fromNativeSeparators(
                relativePath
            )
        );
    }
}

bool ProjectModel::shouldSkipDirectory(
    const QString &directoryName
) const
{
    return kSkippedDirectories.contains(
        directoryName
    );
}

bool ProjectModel::isLikelyTextFile(
    const QString &path
) const
{
    const QFileInfo info(path);

    if (
        info.size() >
        kMaxFileSize
    ) {
        return false;
    }

    const QString suffix =
        info.suffix().toLower();

    if (
        !suffix.isEmpty()
        && kTextExtensions.contains(
            suffix
        )
    ) {
        return true;
    }

    const QString name =
        info.fileName().toLower();

    if (
        name ==
            QStringLiteral("makefile")
        || name ==
            QStringLiteral("dockerfile")
        || name ==
            QStringLiteral("cmakelists.txt")
        || name ==
            QStringLiteral(".gitignore")
        || name ==
            QStringLiteral(".editorconfig")
    ) {
        return true;
    }

    QFile file(path);

    if (
        !file.open(
            QIODevice::ReadOnly
        )
    ) {
        return false;
    }

    const QByteArray sample =
        file.read(4096);

    return !sample.contains(
        '\0'
    );
}

void ProjectModel::setEditorBuffer(
    const QString &relativePath,
    const QString &code
)
{
    const QString normalized =
        normalizeRelativePath(
            relativePath
        );

    if (normalized.isEmpty()) {
        return;
    }

    m_editorBuffers.insert(
        normalized,
        code
    );

    emit fileChanged(
        normalized
    );
}

void ProjectModel::clearEditorBuffer(
    const QString &relativePath
)
{
    const QString normalized =
        normalizeRelativePath(
            relativePath
        );

    m_editorBuffers.remove(
        normalized
    );

    emit fileChanged(
        normalized
    );
}

void ProjectModel::updateWatcher()
{
    const QStringList oldFiles =
        m_watcher.files();

    const QStringList oldDirectories =
        m_watcher.directories();

    if (!oldFiles.isEmpty()) {
        m_watcher.removePaths(
            oldFiles
        );
    }

    if (!oldDirectories.isEmpty()) {
        m_watcher.removePaths(
            oldDirectories
        );
    }

    if (
        m_projectDirectory.isEmpty()
    ) {
        return;
    }

    QStringList directories;

    directories.append(
        m_projectDirectory
    );

    for (
        const QString &relativeFile :
        m_files
    ) {
        const QString absoluteFile =
            QDir(
                m_projectDirectory
            ).absoluteFilePath(
                relativeFile
            );

        const QString directory =
            QFileInfo(
                absoluteFile
            ).absolutePath();

        if (
            !directories.contains(
                directory
            )
        ) {
            directories.append(
                directory
            );
        }
    }

    m_watcher.addPaths(
        directories
    );
}

void ProjectModel::onFileSystemChanged(
    const QString &path
)
{
    Q_UNUSED(path);

    scanProject();
}