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

// ----- Markdown renderer for the side panel -----
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

    escaped = escaped.replace(/```([\s\S]*?)```/g, function (match, code) {
        code = code.replace(/^[^\n]*\n?/, '');
        var html = '<pre><code>' + code + '</code></pre>';
        codeBlocks.push(html);
        return placeholder + (codeBlocks.length - 1) + placeholder;
    });

    escaped = escaped.replace(/`([^`]+)`/g, '<code>$1</code>');
    escaped = escaped.replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>');
    escaped = escaped.replace(/\*([^*]+)\*/g, '<em>$1</em>');
    escaped = escaped.replace(/\n/g, '<br>');
    escaped = escaped.replace(/(<br>|^)- /g, '$1• ');

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
        glyphMargin: false,
        wordWrap: 'off'
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

// ----- Legacy Range Edits -----
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
    if (!Array.isArray(parsed) || parsed.length === 0) return;

    var model = window.editor.getModel();
    var totalLines = model.getLineCount();
    var operations = [];

    for (var i = 0; i < parsed.length; i++) {
        var op = parsed[i];
        var startLine = Number(op.startLine);
        var startColumn = Number(op.startColumn);
        var endLine = Number(op.endLine);
        var endColumn = Number(op.endColumn);

        if (!isFinite(startLine) || !isFinite(startColumn) || !isFinite(endLine) || !isFinite(endColumn)) continue;
        if (startLine < 1 || startColumn < 1 || endLine < 1 || endColumn < 1) continue;
        if (startLine > endLine || (startLine === endLine && startColumn > endColumn)) continue;

        var text = (op.text !== undefined && op.text !== null) ? String(op.text) : '';
        operations.push({
            range: new window.monaco.Range(startLine, startColumn, endLine, endColumn),
            text: text,
            forceMoveMarkers: true
        });
    }

    operations.sort(function (a, b) {
        if (a.range.startLineNumber !== b.range.startLineNumber) {
            return b.range.startLineNumber - a.range.startLineNumber;
        }
        return b.range.startColumn - a.range.startColumn;
    });

    if (operations.length > 0) {
        try {
            window.editor.executeEdits('backend', operations);
            clearAnnotationOverlay();
            notifyBackendCodeChange();
        } catch (e) {
            console.error('[app.js] executeEdits failed:', e);
        }
    }
};

// ----- Aider-style SEARCH/REPLACE engine -----
// ----- Aider-style SEARCH/REPLACE engine -----
// ----- Aider-style SEARCH/REPLACE engine -----
// ----- Aider-style SEARCH/REPLACE engine -----
window.applySearchReplace = function (blocks) {
    console.log('[app.js] applySearchReplace called');
    if (!window.editor || !window.monaco) {
        console.error('[app.js] Editor not initialized');
        return false;
    }

    var parsed = blocks;
    if (typeof blocks === 'string') {
        try {
            parsed = JSON.parse(blocks);
        } catch (e) {
            console.error('[app.js] applySearchReplace JSON.parse failed:', e);
            return false;
        }
    }
    if (!Array.isArray(parsed) || parsed.length === 0) {
        console.warn('[app.js] No blocks to apply');
        return false;
    }

    var model = window.editor.getModel();
    var fullText = model.getValue();
    var operations = [];
    var failed = [];
    var successCount = 0;

    // Normalize helpers
    function normalizeLineEndings(text) {
        return text.replace(/\r\n/g, '\n').replace(/\r/g, '\n');
    }
    function normalizeTrailingWhitespace(text) {
        return text.split('\n').map(l => l.replace(/\s+$/, '')).join('\n');
    }

    for (var i = 0; i < parsed.length; i++) {
        var block = parsed[i];
        var search = block.search != null ? String(block.search) : '';
        var replace = block.replace != null ? String(block.replace) : '';

        // Handle empty search as insertion at beginning
        if (search === '') {
            if (replace === '') {
                console.warn('[app.js] Block', i, ': both search and replace empty, skipping');
                failed.push({ index: i, reason: 'empty search and replace' });
                continue;
            }
            // Insert at position 0
            operations.push({
                range: new window.monaco.Range(1, 1, 1, 1),
                text: replace,
                forceMoveMarkers: true,
                originalIndex: i
            });
            successCount++;
            console.log('[app.js] Block', i, ': inserted at beginning');
            continue;
        }

        var searchIdx = -1;
        var matchedText = '';

        // Strategy 1: Exact match
        searchIdx = fullText.indexOf(search);
        if (searchIdx !== -1) {
            matchedText = search;
            console.log('[app.js] Block', i, ': exact match at', searchIdx);
        }

        // Strategy 2: Normalized line endings
        if (searchIdx === -1) {
            var normFull = normalizeLineEndings(fullText);
            var normSearch = normalizeLineEndings(search);
            var normIdx = normFull.indexOf(normSearch);
            if (normIdx !== -1) {
                // Map back to original indices (approximation, safe for common cases)
                searchIdx = fullText.indexOf(normSearch);
                if (searchIdx !== -1) {
                    matchedText = normSearch;
                }
                console.log('[app.js] Block', i, ': matched after line-ending normalization');
            }
        }

        // Strategy 3: Normalized trailing whitespace
        if (searchIdx === -1) {
            var normFullWS = normalizeTrailingWhitespace(normalizeLineEndings(fullText));
            var normSearchWS = normalizeTrailingWhitespace(normalizeLineEndings(search));
            var wsIdx = normFullWS.indexOf(normSearchWS);
            if (wsIdx !== -1) {
                // Try to find corresponding position in original
                searchIdx = fullText.indexOf(search.trim());
                if (searchIdx !== -1) {
                    matchedText = search.trim();
                }
                console.log('[app.js] Block', i, ': matched after whitespace normalization');
            }
        }

        if (searchIdx === -1) {
            console.error('[app.js] Block', i, ': search text not found');
            console.error('[app.js] Search text:', JSON.stringify(search.substring(0, 200)));
            failed.push({ index: i, reason: 'search not found' });
            continue;
        }

        // Calculate range
        var before = fullText.substring(0, searchIdx);
        var startLine = (before.match(/\n/g) || []).length + 1;
        var lastNl = before.lastIndexOf('\n');
        var startColumn = (lastNl === -1 ? before.length : before.length - lastNl - 1) + 1;

        var endOffset = searchIdx + matchedText.length;
        var afterStart = fullText.substring(0, endOffset);
        var endLine = (afterStart.match(/\n/g) || []).length + 1;
        var lastNlEnd = afterStart.lastIndexOf('\n');
        var endColumn = (lastNlEnd === -1 ? afterStart.length : afterStart.length - lastNlEnd - 1) + 1;

        operations.push({
            range: new window.monaco.Range(startLine, startColumn, endLine, endColumn),
            text: replace,
            forceMoveMarkers: true,
            originalIndex: i
        });
        successCount++;
        console.log('[app.js] Block', i, ': applied');
    }

    // Sort operations bottom-up
    operations.sort(function (a, b) {
        if (a.range.startLineNumber !== b.range.startLineNumber) {
            return b.range.startLineNumber - a.range.startLineNumber;
        }
        return b.range.startColumn - a.range.startColumn;
    });

    if (operations.length > 0) {
        try {
            window.editor.executeEdits('backend-sr', operations);
            clearAnnotationOverlay();
            notifyBackendCodeChange();
            console.log('[app.js] Applied', operations.length, 'edits');
        } catch (e) {
            console.error('[app.js] executeEdits failed:', e);
            return false;
        }
    }

    if (failed.length > 0) {
        console.warn('[app.js]', failed.length, 'blocks failed');
        return false;
    }
    return true;
};
window.clearAnnotations = function () {
    clearAnnotationOverlay();
};

window.setAnnotations = function (annotations) {
    console.log('[app.js] setAnnotations called');
    if (!window.editor || !overlayEl) return;

    var parsedAnnotations = annotations;
    if (typeof annotations === 'string') {
        try {
            parsedAnnotations = JSON.parse(annotations);
        } catch (e) {
            console.error('[app.js] JSON.parse failed:', e);
            return;
        }
    }
    if (!Array.isArray(parsedAnnotations)) return;
    renderAnnotations(parsedAnnotations);
};

window.showAnswer = function (text) {
    showGeneralAnswer(text);
};