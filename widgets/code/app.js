window.backend = null;
window.editor = null;
window.overlayEl = null;

console.log('[app.js] file loaded and parsed successfully');

require.config({
    paths: { vs: 'https://cdnjs.cloudflare.com/ajax/libs/monaco-editor/0.45.0/min/vs' }
});

function notifyBackendCodeChange() {
    if (window.editor && window.backend && typeof window.backend.onCodeChanged === 'function') {
        window.backend.onCodeChanged(window.editor.getValue());
    }
}

// ----- Annotation overlay state -----
var currentAnnotations = [];
var annotationMarkers = [];
var activeBubble = null;
var overlayEl = null;

function clearAnnotationOverlay() {
    if (overlayEl) {
        while (overlayEl.firstChild) {
            overlayEl.removeChild(overlayEl.firstChild);
        }
    }
    annotationMarkers = [];
    activeBubble = null;
}

function updateAnnotationPositions() {
    if (!window.editor || !overlayEl) return;

    annotationMarkers.forEach(function (item) {
        var line = item.annotation.startLine;
        var top = window.editor.getTopForLineNumber(line) - window.editor.getScrollTop();
        item.marker.style.top = top + 'px';
    });
}

function createMarker(annotation) {
    if (!window.editor || !overlayEl) return;

    var marker = document.createElement('div');
    marker.className = 'annotation-marker ' + (annotation.severity || 'info');
    marker.textContent = 'i';

    var top = window.editor.getTopForLineNumber(annotation.startLine) - window.editor.getScrollTop();
    marker.style.top = top + 'px';
    marker.style.left = '10px';

    marker.addEventListener('click', function (event) {
        event.stopPropagation();
        toggleBubble(marker, annotation);
    });

    overlayEl.appendChild(marker);
    annotationMarkers.push({ marker: marker, annotation: annotation });
}

function toggleBubble(marker, annotation) {
    if (activeBubble) {
        activeBubble.remove();
        activeBubble = null;
    }

    var bubble = document.createElement('div');
    bubble.className = 'annotation-bubble visible';

    var closeBtn = document.createElement('button');
    closeBtn.className = 'annotation-close';
    closeBtn.textContent = '×';
    closeBtn.addEventListener('click', function (e) {
        e.stopPropagation();
        bubble.remove();
        activeBubble = null;
    });

    var text = document.createElement('div');
    text.textContent = annotation.message || 'No message';

    bubble.appendChild(closeBtn);
    bubble.appendChild(text);

    var markerRect = marker.getBoundingClientRect();
    var overlayRect = overlayEl.getBoundingClientRect();
    var left = markerRect.left - overlayRect.left + 25;
    var top = markerRect.top - overlayRect.top;

    bubble.style.left = left + 'px';
    bubble.style.top = top + 'px';

    overlayEl.appendChild(bubble);
    activeBubble = bubble;

    setTimeout(function () {
        document.addEventListener('click', function closeHandler(e) {
            if (activeBubble && !activeBubble.contains(e.target) && e.target !== marker) {
                activeBubble.remove();
                activeBubble = null;
                document.removeEventListener('click', closeHandler);
            }
        });
    }, 0);
}

function renderAnnotations(annotations) {
    clearAnnotationOverlay();
    if (!Array.isArray(annotations) || annotations.length === 0) return;

    currentAnnotations = annotations;
    annotations.forEach(function (ann) {
        createMarker(ann);
    });
    updateAnnotationPositions();
}

// ----- Markdown-ish renderer for the side panel -----
function escapeHtml(str) {
    return String(str)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#039;');
}

function markdownToHtml(text) {
    var escaped = escapeHtml(text);
    var codeBlocks = [];
    var placeholder = '%%CODEBLOCK%%';

    // Extract fenced code blocks first
    escaped = escaped.replace(/```([\s\S]*?)```/g, function (match, code) {
        // Remove optional language identifier from first line
        code = code.replace(/^[^\n]*\n?/, '');
        var html = '<pre><code>' + code + '</code></pre>';
        codeBlocks.push(html);
        return placeholder + (codeBlocks.length - 1) + placeholder;
    });

    // Inline code
    escaped = escaped.replace(/`([^`]+)`/g, '<code>$1</code>');

    // Bold
    escaped = escaped.replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>');

    // Italic
    escaped = escaped.replace(/\*([^*]+)\*/g, '<em>$1</em>');

    // Newlines to <br>
    escaped = escaped.replace(/\n/g, '<br>');

    // Simple bullet lines
    escaped = escaped.replace(/(<br>|^)- /g, '$1• ');

    // Restore code blocks
    for (var i = 0; i < codeBlocks.length; i++) {
        escaped = escaped.split(placeholder + i + placeholder).join(codeBlocks[i]);
    }

    return escaped;
}

function showGeneralAnswer(text) {
    var panel = document.getElementById('side-panel');
    var content = document.getElementById('answer-content');
    content.innerHTML = markdownToHtml(text || '');
    panel.classList.add('open');
}

// ----- Monaco setup -----
require(['vs/editor/editor.main'], function () {
    console.log('[app.js] monaco require callback entered');
    window.monaco = monaco;

    window.editor = monaco.editor.create(document.getElementById('editor-container'), {
        value: '// Start typing code here...\n',
        language: 'cpp',
        theme: 'vs-dark',
        automaticLayout: true,
        fontFamily: "'JetBrains Mono', var(--font-mono), monospace",
        fontLigatures: true,
        fontSize: 14,
        minimap: { enabled: true },
        scrollBeyondLastLine: false,
        roundedSelection: true,
        padding: { top: 12, bottom: 12 },
        glyphMargin: false
    });

    overlayEl = document.getElementById('annotation-overlay');

    window.editor.onDidChangeModelContent(function () {
        notifyBackendCodeChange();
        clearAnnotationOverlay();
    });

    window.editor.onDidScrollChange(function () {
        updateAnnotationPositions();
    });

    notifyBackendCodeChange();
    console.log('[app.js] monaco editor created');
});

// ----- WebChannel init -----
function initWebChannel() {
    if (typeof qt !== "undefined" && qt.webChannelTransport && typeof QWebChannel !== "undefined") {
        new QWebChannel(qt.webChannelTransport, function (channel) {
            window.backend = channel.objects.backend;
            notifyBackendCodeChange();
        });
    } else {
        setTimeout(initWebChannel, 50);
    }
}

if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", initWebChannel);
} else {
    initWebChannel();
}

// ----- Editor / exposed functions -----
function setCode(code) {
    if (window.editor) {
        window.editor.setValue(code);
    }
}

function getCode() {
    return window.editor ? window.editor.getValue() : '';
}

window.getCode = getCode;
window.setCode = setCode;

function setLanguage(lang) {
    console.log('[app.js] setLanguage called with:', lang);
    if (window.editor && window.monaco) {
        window.monaco.editor.setModelLanguage(window.editor.getModel(), lang);
    }
}

function setTheme(theme) {
    console.log('[app.js] setTheme called with:', theme);
    if (window.editor && window.monaco) {
        window.editor.updateOptions({ theme: theme });
    }
}

window.setEditorCode = function (code, lang) {
    console.log('[app.js] setEditorCode called with code length:', code ? code.length : 0, 'lang:', lang);
    if (window.editor) {
        var state = window.editor.saveViewState();
        window.editor.setValue(code);
        if (state) {
            window.editor.restoreViewState(state);
        }
        if (lang && lang.trim() !== '') {
            setLanguage(lang);
        }
        clearAnnotationOverlay();
    }
};

window.appendCodeToEditor = function (code) {
    console.log('[app.js] appendCodeToEditor called with code length:', code ? code.length : 0);
    if (window.editor) {
        var model = window.editor.getModel();
        var lastLine = model.getLineCount();
        var lastColumn = model.getLineMaxColumn(lastLine);
        window.editor.executeEdits("backend", [{
            range: new window.monaco.Range(lastLine, lastColumn, lastLine, lastColumn),
            text: code,
            forceMoveMarkers: true
        }]);
    }
};

// ----- applyEdits with validation/clamping -----
window.applyEdits = function (edits) {
    console.log('[app.js] applyEdits called');
    if (!window.editor || !window.monaco) return;

    var parsed = edits;
    if (typeof edits === 'string') {
        try {
            parsed = JSON.parse(edits);
        } catch (e) {
            console.error('[app.js] applyEdits JSON.parse failed:', e);
            return;
        }
    }

    if (!Array.isArray(parsed) || parsed.length === 0) {
        console.log('[app.js] applyEdits: no edits to apply');
        return;
    }

    var model = window.editor.getModel();
    var totalLines = model.getLineCount();

    // Sort by start position
    parsed.sort(function (a, b) {
        var aStartLine = Number(a.startLine) || 1;
        var bStartLine = Number(b.startLine) || 1;
        if (aStartLine !== bStartLine) return aStartLine - bStartLine;
        var aStartCol = Number(a.startColumn) || 1;
        var bStartCol = Number(b.startColumn) || 1;
        return aStartCol - bStartCol;
    });

    var operations = [];
    var skipped = [];

    for (var i = 0; i < parsed.length; i++) {
        var op = parsed[i];
        var startLine = Number(op.startLine);
        var startColumn = Number(op.startColumn);
        var endLine = Number(op.endLine);
        var endColumn = Number(op.endColumn);

        if (!isFinite(startLine) || !isFinite(startColumn) ||
            !isFinite(endLine) || !isFinite(endColumn)) {
            skipped.push({ op: op, reason: 'non-numeric line/column' });
            continue;
        }

        var clampedStartLine = startLine;
        var clampedStartColumn = startColumn;
        var clampedEndLine = endLine;
        var clampedEndColumn = endColumn;

        // Clamp lines to [1, totalLines+1]
        if (clampedStartLine > totalLines + 1) {
            console.warn('[app.js] Clamping startLine', clampedStartLine, 'to EOF', totalLines + 1);
            clampedStartLine = totalLines + 1;
            clampedStartColumn = 1;
        }
        if (clampedEndLine > totalLines + 1) {
            console.warn('[app.js] Clamping endLine', clampedEndLine, 'to EOF', totalLines + 1);
            clampedEndLine = totalLines + 1;
            clampedEndColumn = 1;
        }

        // Clamp columns
        if (clampedStartLine >= 1 && clampedStartLine <= totalLines) {
            var maxColStart = model.getLineMaxColumn(clampedStartLine) + 1;
            if (clampedStartColumn > maxColStart) clampedStartColumn = maxColStart;
        } else if (clampedStartLine === totalLines + 1) {
            clampedStartColumn = 1;
        }

        if (clampedEndLine >= 1 && clampedEndLine <= totalLines) {
            var maxColEnd = model.getLineMaxColumn(clampedEndLine) + 1;
            if (clampedEndColumn > maxColEnd) clampedEndColumn = maxColEnd;
        } else if (clampedEndLine === totalLines + 1) {
            clampedEndColumn = 1;
        }

        if (clampedStartLine < 1 || clampedStartColumn < 1 ||
            clampedEndLine < 1 || clampedEndColumn < 1) {
            skipped.push({ op: op, reason: 'clamped to invalid position' });
            continue;
        }

        if (clampedStartLine > clampedEndLine ||
            (clampedStartLine === clampedEndLine && clampedStartColumn > clampedEndColumn)) {
            skipped.push({ op: op, reason: 'reversed range after clamping' });
            continue;
        }

        var text = (op.text !== undefined && op.text !== null) ? String(op.text) : '';

        operations.push({
            range: new window.monaco.Range(
                clampedStartLine, clampedStartColumn,
                clampedEndLine, clampedEndColumn
            ),
            text: text
        });
    }

    var finalOps = [];
    for (var j = 0; j < operations.length; j++) {
        var current = operations[j];

        if (current.text === '' &&
            current.range.startLineNumber === current.range.endLineNumber &&
            current.range.startColumn === current.range.endColumn) {
            skipped.push({ op: parsed[j], reason: 'zero-length deletion' });
            continue;
        }

        if (finalOps.length > 0) {
            var last = finalOps[finalOps.length - 1].range;
            var currRange = current.range;
            var overlap = (currRange.startLineNumber < last.endLineNumber ||
                (currRange.startLineNumber === last.endLineNumber &&
                    currRange.startColumn < last.endColumn));

            if (overlap) {
                skipped.push({ op: parsed[j], reason: 'overlap with previous edit' });
                continue;
            }
        }

        finalOps.push(current);
    }

    if (finalOps.length > 0) {
        try {
            window.editor.executeEdits('backend', finalOps);
            clearAnnotationOverlay();
            notifyBackendCodeChange();
            console.log('[app.js] Applied', finalOps.length, 'edits');
        } catch (e) {
            console.error('[app.js] executeEdits failed:', e);
        }
    } else {
        console.warn('[app.js] No valid edits to apply. Skipped edits:', skipped);
    }
};

window.clearAnnotations = function () {
    clearAnnotationOverlay();
};

window.setAnnotations = function (annotations) {
    console.log('[app.js] setAnnotations called');
    if (!window.editor || !overlayEl) {
        return;
    }

    var parsedAnnotations = annotations;
    if (typeof annotations === 'string') {
        try {
            parsedAnnotations = JSON.parse(annotations);
        } catch (e) {
            console.error('[app.js] JSON.parse failed:', e);
            return;
        }
    }

    if (!Array.isArray(parsedAnnotations)) {
        return;
    }

    renderAnnotations(parsedAnnotations);
};

window.showAnswer = function (text) {
    showGeneralAnswer(text);
};