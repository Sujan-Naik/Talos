window.backend = null;
window.ttsEnabled = false;
window.ttsBuffer = '';
window.ttsSpeechQueue = [];          // Queue of sentences to speak
window.ttsProcessing = false;        // Whether we are currently speaking
window.ttsSpeechTimer = null;        // Timer to pace the backend requests

window.updateHoleRect = function(x, y, width, height, viewWidth) {
    const spacer = document.getElementById('hole-spacer');
    const border = document.getElementById('hole-border');

    if (!spacer || !border) return;

    if (width <= 0 || height <= 0) {
        spacer.style.display = 'none';
        border.style.display = 'none';
        return;
    }

    const isRightSide = (x + width / 2) > (viewWidth / 2);
    const floatDir = isRightSide ? 'right' : 'left';

    spacer.style.display = 'block';
    spacer.style.float = floatDir;
    spacer.style.width = width + 'px';
    spacer.style.height = height + 'px';
    spacer.style.shapeOutside = 'inset(0px 0px 0px 0px)';
    spacer.style.marginTop = Math.max(0, y) + 'px';

    border.style.display = 'block';
    border.style.left = x + 'px';
    border.style.top = y + 'px';
    border.style.width = width + 'px';
    border.style.height = height + 'px';

    scrollToBottom();
};

function setInputValue(text) {
    const input = document.getElementById('message-input');
    if (input) {
        input.value = text;
        autoResizeInput(input);
    }
}

function setCaptureState(text, enabled, stateClass) {
    const btn = document.getElementById('capture-btn');
    if (!btn) return;
    btn.textContent = text;
    btn.disabled = !enabled;
    btn.className = 'action-btn ' + stateClass;
}

function setMicState(text) {
    const btn = document.getElementById('mic-btn');
    if (btn) btn.textContent = text;
}

function cleanMarkdown(text) {
    return text
        .replace(/```[\s\S]*?```/g, ' Code snippet omitted. ')
        .replace(/`([^`]+)`/g, '$1')
        .replace(/[*_#~\[\]]/g, '')
        .replace(/\s+/g, ' ')
        .trim();
}

// Global hook: if your Qt backend can notify JS when speech ends, call this!
// This will override the fallback JS estimator and make audio spacing perfect.
window.onTtsFinished = function() {
    if (window.ttsSpeechTimer) {
        clearTimeout(window.ttsSpeechTimer);
        window.ttsSpeechTimer = null;
    }
    window.ttsProcessing = false;
    _processSpeechQueue();
};

// Internal function to actually speak a single utterance, invoking onEnd when done
function _speakUtterance(text, onEnd) {
    if (!window.ttsEnabled || !text) {
        if (onEnd) onEnd();
        return;
    }

    const plain = cleanMarkdown(text);
    if (!plain) {
        if (onEnd) onEnd();
        return;
    }

    if (window.backend && typeof window.backend.speak === 'function') {
        window.backend.speak(plain);

        // Kokoro gets overwhelmed if flooded with overlapping requests.
        // We estimate the audio duration: ~75ms per char + 400ms base pause.
        // If your Qt backend triggers window.onTtsFinished(), this timer acts as a failsafe.
        const estimatedMs = plain.length * 75 + 400;
        window.ttsSpeechTimer = setTimeout(() => {
            window.ttsSpeechTimer = null;
            if (onEnd) onEnd();
        }, estimatedMs);

    } else if ('speechSynthesis' in window) {
        const u = new SpeechSynthesisUtterance(plain);
        u.rate = 1.0;
        u.pitch = 1.0;
        u.onend = () => { if (onEnd) onEnd(); };
        u.onerror = () => { if (onEnd) onEnd(); };
        window.speechSynthesis.speak(u);
    } else {
        if (onEnd) onEnd();
    }
}

// Strict Queue Manager: Never process the next item until the current one finishes playing.
function _processSpeechQueue() {
    if (window.ttsProcessing || window.ttsSpeechQueue.length === 0) return;
    window.ttsProcessing = true;

    const nextText = window.ttsSpeechQueue.shift();
    _speakUtterance(nextText, () => {
        window.ttsProcessing = false;
        _processSpeechQueue(); // process remaining items safely
    });
}

function _enqueueSpeech(text) {
    if (!text) return;
    window.ttsSpeechQueue.push(text);
    _processSpeechQueue();
}

function speakText(text, immediate = false) {
    if (immediate) {
        window.ttsSpeechQueue = [];
        window.ttsProcessing = false;
        if (window.ttsSpeechTimer) clearTimeout(window.ttsSpeechTimer);

        _speakUtterance(text, () => {});
        return;
    }
    _enqueueSpeech(text);
}

function queueForSpeech(deltaText, isComplete = false) {
    if (!window.ttsEnabled) return;

    // 1. Accumulate raw streams to prevent breaking formatting or partial words
    if (deltaText) {
        window.ttsBuffer += deltaText;
    }

    // 2. Clean fully closed code blocks first so they don't block sentence detection
    window.ttsBuffer = window.ttsBuffer.replace(/```[\s\S]*?```/g, ' Code snippet omitted. ');

    // 3. Wait until any open markdown block closes before splitting sentences
    if (/```/.test(window.ttsBuffer) && !isComplete) {
        return;
    }

    // 4. Safe sentence detection (match punctuation followed by whitespace/end)
    const sentenceRegex = /([.!?])(?:\s+|$)/g;
    let match;
    let lastIndex = 0;
    const sentences = [];

    while ((match = sentenceRegex.exec(window.ttsBuffer)) !== null) {
        const splitIndex = match.index + match[1].length;
        sentences.push(window.ttsBuffer.substring(lastIndex, splitIndex));
        lastIndex = splitIndex;
    }

    if (sentences.length > 0) {
        for (const raw of sentences) {
            const cleaned = cleanMarkdown(raw);
            if (cleaned) {
                _enqueueSpeech(cleaned);
            }
        }
        window.ttsBuffer = window.ttsBuffer.substring(lastIndex).trimStart();
    } else if (isComplete) {
        // Last chunk payload, sweep up any remaining string without ending punctuation
        const cleaned = cleanMarkdown(window.ttsBuffer);
        if (cleaned) {
            _enqueueSpeech(cleaned);
        }
        window.ttsBuffer = '';
    }
}

function flushTTSBuffer() {
    if (!window.ttsEnabled) return;
    if (window.ttsBuffer.trim()) {
        const cleaned = cleanMarkdown(window.ttsBuffer);
        if (cleaned) {
            _enqueueSpeech(cleaned);
        }
        window.ttsBuffer = '';
    }
}

function stopSpeech() {
    window.ttsBuffer = '';
    window.ttsSpeechQueue = [];
    window.ttsProcessing = false;
    if (window.ttsSpeechTimer) {
        clearTimeout(window.ttsSpeechTimer);
        window.ttsSpeechTimer = null;
    }

    if (window.backend && typeof window.backend.stopSpeech === 'function') {
        window.backend.stopSpeech();
    }
    if ('speechSynthesis' in window) {
        window.speechSynthesis.cancel();
    }
}

function toggleTTS(forcedState) {
    const btn = document.getElementById('tts-btn');
    if (typeof forcedState === 'boolean') {
        window.ttsEnabled = forcedState;
    } else {
        window.ttsEnabled = !window.ttsEnabled;
    }

    if (btn) {
        btn.textContent = window.ttsEnabled ? 'TTS: On' : 'TTS: Off';
        btn.classList.toggle('active', window.ttsEnabled);
        if (window.ttsEnabled) {
            btn.classList.add('bg-green-500', 'text-black');
            btn.classList.remove('bg-gray-600', 'text-white');
        } else {
            btn.classList.remove('bg-green-500', 'text-black');
            btn.classList.add('bg-gray-600', 'text-white');
        }
    }

    if (window.backend && typeof window.backend.onTtsToggled === 'function') {
        window.backend.onTtsToggled(window.ttsEnabled);
    }

    if (!window.ttsEnabled) {
        stopSpeech();
    }
}

function sendMessage() {
    const input = document.getElementById('message-input');
    const text = input.value.trim();

    if (!text) return;

    stopSpeech();

    input.value = '';
    autoResizeInput(input);

    document.getElementById('root-wrapper').classList.remove('expanded-input');
    document.getElementById('expand-btn').textContent = '⤢';

    if (window.backend) {
        if (typeof window.backend.onUserSendMessage === 'function') {
            window.backend.onUserSendMessage(text);
        } else if (typeof window.backend.sendMessage === 'function') {
            window.backend.sendMessage(text);
        }
    }
}

function attachCopyUtils(bubble) {
    bubble.querySelectorAll('pre').forEach((pre) => {
        if (pre.querySelector('.code-copy-btn')) return;

        const copyBtn = document.createElement('button');
        copyBtn.className = 'code-copy-btn';
        copyBtn.textContent = 'Copy';

        copyBtn.addEventListener('click', (e) => {
            e.stopPropagation();
            const code = pre.querySelector('code');
            const textToCopy = code ? code.innerText : pre.innerText;

            navigator.clipboard.writeText(textToCopy).then(() => {
                copyBtn.textContent = 'Copied!';
                copyBtn.classList.add('copied');
                setTimeout(() => {
                    copyBtn.textContent = 'Copy';
                    copyBtn.classList.remove('copied');
                }, 2000);
            });
        });

        pre.appendChild(copyBtn);
    });
}

function createMsgCopyButton(textToCopy) {
    const btn = document.createElement('button');
    btn.className = 'msg-copy-btn';
    btn.textContent = 'Copy text';

    btn.addEventListener('click', () => {
        navigator.clipboard.writeText(textToCopy).then(() => {
            btn.textContent = 'Copied text!';
            setTimeout(() => {
                btn.textContent = 'Copy text';
            }, 2000);
        });
    });

    return btn;
}

function appendMessage(text, isUser) {
    const messagesDiv = document.getElementById('messages');
    if (!messagesDiv) return;

    const row = document.createElement('div');
    row.className = 'message-row ' + (isUser ? 'user' : 'ai');

    const bubble = document.createElement('div');
    bubble.className = 'bubble';
    bubble.dataset.rawText = text;

    bubble.innerHTML = marked.parse(text);
    bubble.querySelectorAll('pre code').forEach((block) => {
        hljs.highlightElement(block);
    });

    attachCopyUtils(bubble);

    const msgCopyBtn = createMsgCopyButton(text);

    row.appendChild(bubble);
    row.appendChild(msgCopyBtn);
    messagesDiv.appendChild(row);

    if (!isUser && window.ttsEnabled) {
        finalizeAiMessage();
    }

    scrollToBottom();
}

function appendToLastAiMessage(deltaText) {
    const messagesDiv = document.getElementById('messages');
    if (!messagesDiv) return;

    let aiRows = messagesDiv.getElementsByClassName('message-row ai');
    if (aiRows.length === 0) {
        // FIX: Instead of calling appendMessage (which flushes empty TTS buffers),
        // we directly build the empty AI container so the first token is preserved.
        const row = document.createElement('div');
        row.className = 'message-row ai';
        const bubble = document.createElement('div');
        bubble.className = 'bubble';
        bubble.dataset.rawText = '';
        row.appendChild(bubble);
        messagesDiv.appendChild(row);
        aiRows = messagesDiv.getElementsByClassName('message-row ai');
    }

    const lastAiRow = aiRows[aiRows.length - 1];
    const bubble = lastAiRow.querySelector('.bubble');
    if (bubble) {
        if (!bubble.dataset.rawText) {
            bubble.dataset.rawText = bubble.textContent || "";
        }
        bubble.dataset.rawText += deltaText;
        bubble.innerHTML = marked.parse(bubble.dataset.rawText);
        bubble.querySelectorAll('pre code').forEach((block) => {
            hljs.highlightElement(block);
        });

        attachCopyUtils(bubble);

        let msgCopyBtn = lastAiRow.querySelector('.msg-copy-btn');
        if (msgCopyBtn) {
            msgCopyBtn.replaceWith(createMsgCopyButton(bubble.dataset.rawText));
        } else {
            lastAiRow.appendChild(createMsgCopyButton(bubble.dataset.rawText));
        }

        if (window.ttsEnabled) {
            // Because we fixed row generation above, the very first token now properly triggers here.
            queueForSpeech(deltaText, false);
        }
    }

    scrollToBottom();
}

function finalizeAiMessage() {
    if (window.ttsEnabled) {
        flushTTSBuffer();
    }
}

function scrollToBottom() {
    const container = document.getElementById('chat-container');
    if (container) {
        container.scrollTop = container.scrollHeight;
    }
}

function autoResizeInput(textarea) {
    if (document.getElementById('root-wrapper').classList.contains('expanded-input')) return;
    textarea.style.height = 'auto';
    textarea.style.height = Math.min(textarea.scrollHeight, 250) + 'px';
}

document.addEventListener("DOMContentLoaded", function () {
    const sendBtn = document.getElementById('send-btn');
    const captureBtn = document.getElementById('capture-btn');
    const micBtn = document.getElementById('mic-btn');
    const ttsBtn = document.getElementById('tts-btn');
    const expandBtn = document.getElementById('expand-btn');
    const input = document.getElementById('message-input');
    const wrapper = document.getElementById('root-wrapper');

    input.addEventListener('input', function() {
        autoResizeInput(this);
    });

    expandBtn.addEventListener('click', function() {
        wrapper.classList.toggle('expanded-input');
        const isExpanded = wrapper.classList.contains('expanded-input');
        expandBtn.textContent = isExpanded ? '⤡' : '⤢';
        if (!isExpanded) {
            autoResizeInput(input);
        } else {
            input.style.height = '';
        }
    });

    sendBtn.addEventListener('click', function() {
        sendMessage();
    });

    captureBtn.addEventListener('click', function() {
        if (window.backend && typeof window.backend.requestCapture === 'function') {
            window.backend.requestCapture();
        }
    });

    micBtn.addEventListener('click', function() {
        if (window.backend && typeof window.backend.requestToggleMic === 'function') {
            window.backend.requestToggleMic();
        }
    });

    ttsBtn.addEventListener('click', function() {
        toggleTTS();
    });

    input.addEventListener('keydown', function(e) {
        if (e.key === 'Enter' && (e.ctrlKey || e.metaKey || wrapper.classList.contains('expanded-input'))) {
            return;
        }
        if (e.key === 'Enter' && !e.shiftKey) {
            e.preventDefault();
            sendMessage();
        }
    });

    if (typeof QWebChannel !== "undefined") {
        new QWebChannel(qt.webChannelTransport, function (channel) {
            window.backend = channel.objects.backend;
        });
    }
});