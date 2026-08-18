window.backend = null;
window.project = null;

window.editor = null;
window.overlayEl = null;

window.projectTree = [];
window.currentFilePath = '';

/*
 * Review annotations are kept in application memory.
 *
 * They may refer to multiple project files:
 *
 *   src/rendering/Camera.cpp
 *   include/rendering/Camera.h
 *
 * Monaco only receives the annotations belonging to the
 * currently open file.
 */
var allAnnotations = [];

console.log(
    '[code.js] file loaded and parsed successfully'
);

require.config({
    paths: {
        vs: 'https://cdnjs.cloudflare.com/ajax/libs/monaco-editor/0.45.0/min/vs'
    }
});

// ------------------------------------------------------------
// Backend synchronization
// ------------------------------------------------------------

function notifyBackendCodeChange() {
    if (
        window.editor &&
        window.backend &&
        typeof window.backend.onCodeChanged ===
        'function'
    ) {
        window.backend.onCodeChanged(
            window.editor.getValue()
        );
    }
}

// ------------------------------------------------------------
// Path helpers
// ------------------------------------------------------------

function normalizeProjectPath(value) {
    if (!value) {
        return '';
    }

    return String(value)
        .replace(/\\/g, '/')
        .replace(/^\.\/+/, '');
}

function sameProjectPath(a, b) {
    return (
        normalizeProjectPath(a) ===
        normalizeProjectPath(b)
    );
}

// ------------------------------------------------------------
// Monaco-native review markers
// ------------------------------------------------------------

function clearCurrentMonacoMarkers() {
    if (
        !window.editor ||
        !window.monaco
    ) {
        return;
    }

    var model =
        window.editor.getModel();

    if (!model) {
        return;
    }

    window.monaco.editor.setModelMarkers(
        model,
        'talos-review',
        []
    );
}

function annotationSeverityToMonaco(
    severity
) {
    if (!window.monaco) {
        return 8;
    }

    switch (
        String(
            severity || 'info'
        ).toLowerCase()
        ) {
        case 'error':
            return (
                window.monaco.MarkerSeverity.Error
            );

        case 'warning':
            return (
                window.monaco.MarkerSeverity.Warning
            );

        case 'info':
        default:
            return (
                window.monaco.MarkerSeverity.Info
            );
    }
}

function getAnnotationFile(
    annotation
) {
    if (
        !annotation ||
        !annotation.file
    ) {
        return '';
    }

    return normalizeProjectPath(
        annotation.file
    );
}

function getAnnotationsForCurrentFile() {
    if (
        !window.currentFilePath
    ) {
        return [];
    }

    return allAnnotations.filter(
        function (annotation) {
            return sameProjectPath(
                getAnnotationFile(
                    annotation
                ),
                window.currentFilePath
            );
        }
    );
}

function clampLine(
    model,
    line
) {
    var value =
        Number(line);

    if (
        !isFinite(value)
    ) {
        value = 1;
    }

    return Math.max(
        1,
        Math.min(
            model.getLineCount(),
            Math.floor(value)
        )
    );
}

function clampColumn(
    model,
    line,
    column
) {
    var value =
        Number(column);

    if (
        !isFinite(value)
    ) {
        value = 1;
    }

    var maxColumn =
        model.getLineMaxColumn(
            line
        );

    return Math.max(
        1,
        Math.min(
            maxColumn,
            Math.floor(value)
        )
    );
}

function applyCurrentFileAnnotations() {
    if (
        !window.editor ||
        !window.monaco
    ) {
        return;
    }

    var model =
        window.editor.getModel();

    if (!model) {
        return;
    }

    var annotations =
        getAnnotationsForCurrentFile();

    var markers = [];

    for (
        var i = 0;
        i < annotations.length;
        ++i
    ) {
        var annotation =
            annotations[i];

        var startLine =
            clampLine(
                model,
                annotation.startLine
            );

        var endLine =
            clampLine(
                model,
                annotation.endLine ||
                startLine
            );

        if (
            endLine < startLine
        ) {
            endLine =
                startLine;
        }

        var startColumn =
            clampColumn(
                model,
                startLine,
                annotation.startColumn
            );

        var endColumn =
            clampColumn(
                model,
                endLine,
                annotation.endColumn
            );

        /*
         * Monaco expects the end position to be at or after
         * the start position.
         */
        if (
            endLine === startLine &&
            endColumn < startColumn
        ) {
            endColumn =
                startColumn;
        }

        markers.push({
            severity:
                annotationSeverityToMonaco(
                    annotation.severity
                ),

            message:
                String(
                    annotation.message ||
                    'No message'
                ),

            startLineNumber:
            startLine,

            startColumn:
            startColumn,

            endLineNumber:
            endLine,

            endColumn:
            endColumn,

            source:
                'Talos'
        });
    }

    console.log(
        '[code.js] Applying',
        markers.length,
        'Monaco markers for:',
        window.currentFilePath
    );

    window.monaco.editor.setModelMarkers(
        model,
        'talos-review',
        markers
    );
}

function clearAnnotationOverlay() {
    /*
     * Kept as a compatibility function because existing C++
     * code calls window.clearAnnotations().
     *
     * There is no DOM overlay anymore. We simply clear the
     * current Monaco markers.
     */
    clearCurrentMonacoMarkers();
}

window.clearAnnotations =
    function () {
        console.log(
            '[code.js] Clearing all review annotations'
        );

        allAnnotations = [];

        clearCurrentMonacoMarkers();
    };

window.setAnnotations =
    function (annotations) {
        var parsed =
            annotations;

        if (
            typeof annotations ===
            'string'
        ) {
            try {
                parsed =
                    JSON.parse(
                        annotations
                    );
            } catch (error) {
                console.error(
                    '[code.js] annotation parse failed:',
                    error
                );

                return;
            }
        }

        if (
            !Array.isArray(parsed)
        ) {
            console.error(
                '[code.js] setAnnotations expected an array:',
                parsed
            );

            return;
        }

        console.log(
            '[code.js] Received',
            parsed.length,
            'annotations'
        );

        console.log(
            '[code.js] Current file:',
            window.currentFilePath
        );

        allAnnotations =
            parsed.map(
                function (annotation) {
                    return Object.assign(
                        {},
                        annotation
                    );
                }
            );

        applyCurrentFileAnnotations();
    };

window.getAnnotations =
    function () {
        return allAnnotations.slice();
    };

// ------------------------------------------------------------
// Project tree
// ------------------------------------------------------------

window.setProjectTree =
    function (tree) {
        if (
            !Array.isArray(tree)
        ) {
            return;
        }

        window.projectTree =
            tree;

        renderProjectTree();
    };

function createProjectTreeNode(
    node
) {
    var li =
        document.createElement(
            'li'
        );

    li.className =
        'project-tree-item';

    var row =
        document.createElement(
            'button'
        );

    row.type =
        'button';

    row.className =
        'project-tree-row';

    var chevron =
        document.createElement(
            'span'
        );

    chevron.className =
        'project-tree-chevron';

    var icon =
        document.createElement(
            'span'
        );

    icon.className =
        'project-tree-icon';

    var label =
        document.createElement(
            'span'
        );

    label.className =
        'project-tree-label';

    label.textContent =
        node.name || '';

    // --------------------------------------------------------
    // Directory
    // --------------------------------------------------------

    if (
        node.type ===
        'directory'
    ) {
        chevron.textContent =
            '▸';

        icon.textContent =
            '📁';

        var children =
            document.createElement(
                'ul'
            );

        children.className =
            'project-tree-children';

        var childNodes =
            Array.isArray(
                node.children
            )
                ? node.children
                : [];

        childNodes.forEach(
            function (child) {
                children.appendChild(
                    createProjectTreeNode(
                        child
                    )
                );
            }
        );

        children.style.display =
            'none';

        row.addEventListener(
            'click',
            function (event) {
                event.stopPropagation();

                var expanded =
                    children.style.display !==
                    'none';

                children.style.display =
                    expanded
                        ? 'none'
                        : 'block';

                chevron.textContent =
                    expanded
                        ? '▸'
                        : '▾';
            }
        );

        row.appendChild(
            chevron
        );

        row.appendChild(
            icon
        );

        row.appendChild(
            label
        );

        li.appendChild(
            row
        );

        li.appendChild(
            children
        );

        return li;
    }

    // --------------------------------------------------------
    // File
    // --------------------------------------------------------

    icon.textContent =
        '📄';

    if (
        sameProjectPath(
            node.path,
            window.currentFilePath
        )
    ) {
        row.classList.add(
            'active'
        );
    }

    row.addEventListener(
        'click',
        function (event) {
            event.stopPropagation();

            console.log(
                '[code.js] Opening project file:',
                node.path
            );

            if (
                window.backend &&
                typeof window.backend.requestOpenFile ===
                'function'
            ) {
                window.backend.requestOpenFile(
                    node.path
                );
            }
        }
    );

    row.appendChild(
        chevron
    );

    row.appendChild(
        icon
    );

    row.appendChild(
        label
    );

    li.appendChild(
        row
    );

    return li;
}

function renderProjectTree() {
    var container =
        document.getElementById(
            'project-files-list'
        );

    if (!container) {
        return;
    }

    container.innerHTML =
        '';

    var tree =
        document.createElement(
            'ul'
        );

    tree.className =
        'project-tree';

    window.projectTree.forEach(
        function (node) {
            tree.appendChild(
                createProjectTreeNode(
                    node
                )
            );
        }
    );

    container.appendChild(
        tree
    );
}

window.setCurrentFile =
    function (filePath) {
        var normalized =
            normalizeProjectPath(
                filePath
            );

        console.log(
            '[code.js] setCurrentFile:',
            normalized
        );

        window.currentFilePath =
            normalized;

        renderProjectTree();

        /*
         * Important:
         * switching files changes the active Monaco model
         * contents, so immediately apply the annotations that
         * belong to the new file.
         */
        applyCurrentFileAnnotations();
    };

// ------------------------------------------------------------
// Persistent chat
// ------------------------------------------------------------

function ensureChatVisible() {
    var panel =
        document.getElementById(
            'side-panel'
        );

    if (panel) {
        panel.classList.add(
            'open'
        );
    }
}

function appendChatMessage(
    text,
    isUser
) {
    var container =
        document.getElementById(
            'answer-content'
        );

    if (!container) {
        return;
    }

    var empty =
        container.querySelector(
            '.chat-empty'
        );

    if (empty) {
        empty.remove();
    }

    var message =
        document.createElement(
            'div'
        );

    message.className =
        'chat-message ' +
        (
            isUser
                ? 'user'
                : 'assistant'
        );

    var bubble =
        document.createElement(
            'div'
        );

    bubble.className =
        'chat-bubble';

    bubble.innerHTML =
        isUser
            ? escapeHtml(
                text
            ).replace(
                /\n/g,
                '<br>'
            )
            : markdownToHtml(
                text
            );

    message.appendChild(
        bubble
    );

    container.appendChild(
        message
    );

    ensureChatVisible();

    scrollChatToBottom();
}

function restoreChat(
    messages
) {
    var container =
        document.getElementById(
            'answer-content'
        );

    if (!container) {
        return;
    }

    container.innerHTML =
        '';

    if (
        !Array.isArray(messages) ||
        messages.length === 0
    ) {
        var empty =
            document.createElement(
                'div'
            );

        empty.className =
            'chat-empty';

        empty.textContent =
            'Ask about the code or review the current project.';

        container.appendChild(
            empty
        );

        return;
    }

    messages.forEach(
        function (message) {
            var role =
                message.role;

            if (
                role !== 'user' &&
                role !== 'assistant'
            ) {
                return;
            }

            appendChatMessage(
                message.content || '',
                role === 'user'
            );
        }
    );

    ensureChatVisible();

    scrollChatToBottom();
}

function clearChat() {
    var container =
        document.getElementById(
            'answer-content'
        );

    if (!container) {
        return;
    }

    container.innerHTML =
        '';

    var empty =
        document.createElement(
            'div'
        );

    empty.className =
        'chat-empty';

    empty.textContent =
        'Ask about the code or review the current project.';

    container.appendChild(
        empty
    );
}

function scrollChatToBottom() {
    var container =
        document.getElementById(
            'answer-content'
        );

    if (!container) {
        return;
    }

    container.scrollTop =
        container.scrollHeight;
}

window.appendChatMessage =
    appendChatMessage;

window.restoreChat =
    restoreChat;

window.clearChat =
    clearChat;

window.scrollChatToBottom =
    scrollChatToBottom;

// ------------------------------------------------------------
// Monaco setup
// ------------------------------------------------------------

require(
    ['vs/editor/editor.main'],
    function () {
        console.log(
            '[code.js] monaco require callback entered'
        );

        window.monaco =
            monaco;

        window.editor =
            monaco.editor.create(
                document.getElementById(
                    'editor-container'
                ),
                {
                    value:
                        '// Start typing code here...\n',

                    language:
                        'cpp',

                    theme:
                        'vs-dark',

                    automaticLayout:
                        true,

                    fontFamily:
                        "'JetBrains Mono', var(--font-mono), monospace",

                    fontLigatures:
                        true,

                    fontSize:
                        14,

                    minimap: {
                        enabled: true
                    },

                    scrollBeyondLastLine:
                        false,

                    roundedSelection:
                        true,

                    padding: {
                        top: 12,
                        bottom: 12
                    },

                    /*
                     * Enable the gutter used by Monaco's native
                     * diagnostics/markers.
                     */
                    glyphMargin:
                        true,

                    wordWrap:
                        'off'
                }
            );

        /*
         * We no longer need the custom annotation overlay.
         * Keep the element hidden from pointer interaction in
         * case older HTML/CSS still contains it.
         */
        window.overlayEl =
            document.getElementById(
                'annotation-overlay'
            );

        if (window.overlayEl) {
            window.overlayEl.style.display =
                'none';
        }

        window.editor.onDidChangeModelContent(
            function () {
                notifyBackendCodeChange();

                /*
                 * Do not discard allAnnotations here.
                 * The review belongs to the project, while the
                 * text model may have been modified independently.
                 *
                 * The current file's markers are cleared because
                 * their locations may no longer correspond to the
                 * modified text.
                 */
                clearCurrentMonacoMarkers();
            }
        );

        window.editor.onDidChangeModel(
            function () {
                /*
                 * Reapply review markers when Monaco's model changes.
                 */
                applyCurrentFileAnnotations();
            }
        );

        notifyBackendCodeChange();

        clearChat();

        console.log(
            '[code.js] monaco editor created'
        );

        if (
            window.currentFilePath
        ) {
            applyCurrentFileAnnotations();
        }
    }
);

// ------------------------------------------------------------
// Markdown
// ------------------------------------------------------------

function escapeHtml(
    str
) {
    return String(str)
        .replace(
            /&/g,
            '&amp;'
        )
        .replace(
            /</g,
            '&lt;'
        )
        .replace(
            />/g,
            '&gt;'
        )
        .replace(
            /"/g,
            '&quot;'
        )
        .replace(
            /'/g,
            '&#039;'
        );
}

function markdownToHtml(
    text
) {
    var escaped =
        escapeHtml(text);

    var codeBlocks = [];

    var placeholder =
        '%%CODEBLOCK%%';

    escaped =
        escaped.replace(
            /```([\s\S]*?)```/g,
            function (
                match,
                code
            ) {
                code =
                    code.replace(
                        /^[^\n]*\n?/,
                        ''
                    );

                var html =
                    '<pre><code>' +
                    code +
                    '</code></pre>';

                codeBlocks.push(
                    html
                );

                return (
                    placeholder +
                    (
                        codeBlocks.length -
                        1
                    ) +
                    placeholder
                );
            }
        );

    escaped =
        escaped.replace(
            /`([^`]+)`/g,
            '<code>$1</code>'
        );

    escaped =
        escaped.replace(
            /\*\*([^*]+)\*\*/g,
            '<strong>$1</strong>'
        );

    escaped =
        escaped.replace(
            /\*([^*]+)\*/g,
            '<em>$1</em>'
        );

    escaped =
        escaped.replace(
            /\n/g,
            '<br>'
        );

    escaped =
        escaped.replace(
            /(<br>|^)- /g,
            '$1• '
        );

    for (
        var i = 0;
        i < codeBlocks.length;
        ++i
    ) {
        escaped =
            escaped
                .split(
                    placeholder +
                    i +
                    placeholder
                )
                .join(
                    codeBlocks[i]
                );
    }

    return escaped;
}

// ------------------------------------------------------------
// WebChannel
// ------------------------------------------------------------

function initWebChannel() {
    if (
        typeof qt !== 'undefined' &&
        qt.webChannelTransport &&
        typeof QWebChannel !== 'undefined'
    ) {
        new QWebChannel(
            qt.webChannelTransport,
            function (channel) {
                window.backend =
                    channel.objects.backend;

                window.project =
                    channel.objects.project;

                if (
                    window.project &&
                    window.project.projectTreeChanged
                ) {
                    window.project.projectTreeChanged.connect(
                        function (tree) {
                            window.setProjectTree(
                                tree
                            );
                        }
                    );
                }

                notifyBackendCodeChange();
            }
        );

        return;
    }

    setTimeout(
        initWebChannel,
        50
    );
}

if (
    document.readyState ===
    'loading'
) {
    document.addEventListener(
        'DOMContentLoaded',
        initWebChannel
    );
} else {
    initWebChannel();
}

// ------------------------------------------------------------
// Editor API
// ------------------------------------------------------------

function setCode(
    code
) {
    if (window.editor) {
        window.editor.setValue(
            code
        );
    }
}

function getCode() {
    return window.editor
        ? window.editor.getValue()
        : '';
}

window.getCode =
    getCode;

window.setCode =
    setCode;

function setLanguage(
    lang
) {
    console.log(
        '[code.js] setLanguage called with:',
        lang
    );

    if (
        window.editor &&
        window.monaco
    ) {
        window.monaco.editor.setModelLanguage(
            window.editor.getModel(),
            lang
        );
    }
}

function setTheme(
    theme
) {
    console.log(
        '[code.js] setTheme called with:',
        theme
    );

    if (
        window.monaco
    ) {
        window.monaco.editor.setTheme(
            theme
        );
    }
}

window.setEditorCode =
    function (
        code,
        lang
    ) {
        console.log(
            '[code.js] setEditorCode called with code length:',
            code ? code.length : 0,
            'lang:',
            lang
        );

        if (!window.editor) {
            return;
        }

        var state =
            window.editor.saveViewState();

        /*
         * Clear markers before replacing the model text.
         * After the content change, setCurrentFile/applyCurrentFileAnnotations
         * will restore the appropriate review markers.
         */
        clearCurrentMonacoMarkers();

        window.editor.setValue(
            code
        );

        if (state) {
            window.editor.restoreViewState(
                state
            );
        }

        if (
            lang &&
            lang.trim() !== ''
        ) {
            setLanguage(
                lang
            );
        }

        /*
         * Setting the value fires onDidChangeModelContent,
         * so wait until Monaco has completed the update before
         * restoring the current file's annotations.
         */
        setTimeout(
            function () {
                applyCurrentFileAnnotations();
            },
            0
        );
    };

window.appendCodeToEditor =
    function (
        code
    ) {
        console.log(
            '[code.js] appendCodeToEditor called with code length:',
            code ? code.length : 0
        );

        if (!window.editor) {
            return;
        }

        var model =
            window.editor.getModel();

        var lastLine =
            model.getLineCount();

        var lastColumn =
            model.getLineMaxColumn(
                lastLine
            );

        window.editor.executeEdits(
            'backend',
            [{
                range:
                    new window.monaco.Range(
                        lastLine,
                        lastColumn,
                        lastLine,
                        lastColumn
                    ),

                text:
                code,

                forceMoveMarkers:
                    true
            }]
        );

        notifyBackendCodeChange();
    };

// ------------------------------------------------------------
// Range edits
// ------------------------------------------------------------

window.applyEdits =
    function (
        edits
    ) {
        if (
            !window.editor ||
            !window.monaco
        ) {
            return;
        }

        var parsed =
            edits;

        if (
            typeof edits ===
            'string'
        ) {
            try {
                parsed =
                    JSON.parse(
                        edits
                    );
            } catch (error) {
                console.error(
                    '[code.js] applyEdits JSON.parse failed:',
                    error
                );

                return;
            }
        }

        if (
            !Array.isArray(parsed) ||
            parsed.length === 0
        ) {
            return;
        }

        var operations = [];

        for (
            var i = 0;
            i < parsed.length;
            ++i
        ) {
            var operation =
                parsed[i];

            var startLine =
                Number(
                    operation.startLine
                );

            var startColumn =
                Number(
                    operation.startColumn
                );

            var endLine =
                Number(
                    operation.endLine
                );

            var endColumn =
                Number(
                    operation.endColumn
                );

            if (
                !isFinite(startLine) ||
                !isFinite(startColumn) ||
                !isFinite(endLine) ||
                !isFinite(endColumn)
            ) {
                continue;
            }

            if (
                startLine < 1 ||
                startColumn < 1 ||
                endLine < 1 ||
                endColumn < 1
            ) {
                continue;
            }

            if (
                startLine > endLine ||
                (
                    startLine === endLine &&
                    startColumn > endColumn
                )
            ) {
                continue;
            }

            operations.push({
                range:
                    new window.monaco.Range(
                        startLine,
                        startColumn,
                        endLine,
                        endColumn
                    ),

                text:
                    operation.text !== undefined &&
                    operation.text !== null
                        ? String(
                            operation.text
                        )
                        : '',

                forceMoveMarkers:
                    true
            });
        }

        operations.sort(
            function (
                a,
                b
            ) {
                if (
                    a.range.startLineNumber !==
                    b.range.startLineNumber
                ) {
                    return (
                        b.range.startLineNumber -
                        a.range.startLineNumber
                    );
                }

                return (
                    b.range.startColumn -
                    a.range.startColumn
                );
            }
        );

        if (
            operations.length > 0
        ) {
            try {
                window.editor.executeEdits(
                    'backend',
                    operations
                );

                clearCurrentMonacoMarkers();

                notifyBackendCodeChange();
            } catch (error) {
                console.error(
                    '[code.js] executeEdits failed:',
                    error
                );
            }
        }
    };

// ------------------------------------------------------------
// SEARCH / REPLACE
// ------------------------------------------------------------

window.applySearchReplace =
    function (
        blocks
    ) {
        console.log(
            '[code.js] applySearchReplace called'
        );

        if (
            !window.editor ||
            !window.monaco
        ) {
            console.error(
                '[code.js] Editor not initialized'
            );

            return false;
        }

        var parsed =
            blocks;

        if (
            typeof blocks ===
            'string'
        ) {
            try {
                parsed =
                    JSON.parse(
                        blocks
                    );
            } catch (error) {
                console.error(
                    '[code.js] applySearchReplace JSON.parse failed:',
                    error
                );

                return false;
            }
        }

        if (
            !Array.isArray(parsed) ||
            parsed.length === 0
        ) {
            console.warn(
                '[code.js] No blocks to apply'
            );

            return false;
        }

        var model =
            window.editor.getModel();

        var fullText =
            model.getValue();

        var operations = [];
        var failed = [];
        var successCount = 0;

        function normalizeLineEndings(
            text
        ) {
            return text
                .replace(
                    /\r\n/g,
                    '\n'
                )
                .replace(
                    /\r/g,
                    '\n'
                );
        }

        function normalizeTrailingWhitespace(
            text
        ) {
            return text
                .split('\n')
                .map(
                    function (line) {
                        return line.replace(
                            /\s+$/,
                            ''
                        );
                    }
                )
                .join('\n');
        }

        for (
            var i = 0;
            i < parsed.length;
            ++i
        ) {
            var block =
                parsed[i];

            var search =
                block.search != null
                    ? String(
                        block.search
                    )
                    : '';

            var replace =
                block.replace != null
                    ? String(
                        block.replace
                    )
                    : '';

            if (
                search === ''
            ) {
                if (
                    replace === ''
                ) {
                    failed.push({
                        index:
                        i,
                        reason:
                            'empty search and replace'
                    });

                    continue;
                }

                operations.push({
                    range:
                        new window.monaco.Range(
                            1,
                            1,
                            1,
                            1
                        ),

                    text:
                    replace,

                    forceMoveMarkers:
                        true
                });

                successCount++;

                continue;
            }

            var searchIdx =
                -1;

            var matchedText =
                '';

            searchIdx =
                fullText.indexOf(
                    search
                );

            if (
                searchIdx !==
                -1
            ) {
                matchedText =
                    search;
            }

            if (
                searchIdx ===
                -1
            ) {
                var normFull =
                    normalizeLineEndings(
                        fullText
                    );

                var normSearch =
                    normalizeLineEndings(
                        search
                    );

                var normIdx =
                    normFull.indexOf(
                        normSearch
                    );

                if (
                    normIdx !==
                    -1
                ) {
                    searchIdx =
                        fullText.indexOf(
                            normSearch
                        );

                    if (
                        searchIdx !==
                        -1
                    ) {
                        matchedText =
                            normSearch;
                    }
                }
            }

            if (
                searchIdx ===
                -1
            ) {
                var normFullWS =
                    normalizeTrailingWhitespace(
                        normalizeLineEndings(
                            fullText
                        )
                    );

                var normSearchWS =
                    normalizeTrailingWhitespace(
                        normalizeLineEndings(
                            search
                        )
                    );

                var wsIdx =
                    normFullWS.indexOf(
                        normSearchWS
                    );

                if (
                    wsIdx !==
                    -1
                ) {
                    searchIdx =
                        fullText.indexOf(
                            search.trim()
                        );

                    if (
                        searchIdx !==
                        -1
                    ) {
                        matchedText =
                            search.trim();
                    }
                }
            }

            if (
                searchIdx ===
                -1
            ) {
                failed.push({
                    index:
                    i,
                    reason:
                        'search not found'
                });

                continue;
            }

            var before =
                fullText.substring(
                    0,
                    searchIdx
                );

            var startLine =
                (
                    before.match(
                        /\n/g
                    ) || []
                ).length + 1;

            var lastNl =
                before.lastIndexOf(
                    '\n'
                );

            var startColumn =
                (
                    lastNl === -1
                        ? before.length
                        : before.length -
                        lastNl -
                        1
                ) + 1;

            var endOffset =
                searchIdx +
                matchedText.length;

            var throughEnd =
                fullText.substring(
                    0,
                    endOffset
                );

            var endLine =
                (
                    throughEnd.match(
                        /\n/g
                    ) || []
                ).length + 1;

            var lastNlEnd =
                throughEnd.lastIndexOf(
                    '\n'
                );

            var endColumn =
                (
                    lastNlEnd === -1
                        ? throughEnd.length
                        : throughEnd.length -
                        lastNlEnd -
                        1
                ) + 1;

            operations.push({
                range:
                    new window.monaco.Range(
                        startLine,
                        startColumn,
                        endLine,
                        endColumn
                    ),

                text:
                replace,

                forceMoveMarkers:
                    true
            });

            successCount++;
        }

        operations.sort(
            function (
                a,
                b
            ) {
                if (
                    a.range.startLineNumber !==
                    b.range.startLineNumber
                ) {
                    return (
                        b.range.startLineNumber -
                        a.range.startLineNumber
                    );
                }

                return (
                    b.range.startColumn -
                    a.range.startColumn
                );
            }
        );

        if (
            operations.length > 0
        ) {
            try {
                window.editor.executeEdits(
                    'backend-sr',
                    operations
                );

                clearCurrentMonacoMarkers();

                notifyBackendCodeChange();

                console.log(
                    '[code.js] Applied',
                    operations.length,
                    'edits'
                );
            } catch (error) {
                console.error(
                    '[code.js] executeEdits failed:',
                    error
                );

                return false;
            }
        }

        if (
            failed.length > 0
        ) {
            console.warn(
                '[code.js]',
                failed.length,
                'blocks failed'
            );

            return false;
        }

        return successCount > 0;
    };

// ------------------------------------------------------------
// Answer
// ------------------------------------------------------------

window.showAnswer =
    function (text) {
        ensureChatVisible();
        scrollChatToBottom();
    };