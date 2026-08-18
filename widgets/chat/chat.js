window.backend = null;
window.ttsEnabled = false;
window.ttsVoice = 'af_bella';
window.ttsVoices = [];

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

function sendMessage() {
    const input = document.getElementById('message-input');
    const text = input.value.trim();

    if (!text) return;

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

    scrollToBottom();
}

function appendToLastAiMessage(deltaText) {
    const messagesDiv = document.getElementById('messages');
    if (!messagesDiv) return;

    let aiRows = messagesDiv.getElementsByClassName('message-row ai');
    if (aiRows.length === 0) {
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
    }

    scrollToBottom();
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

function setTtsUiState(enabled) {
    window.ttsEnabled = !!enabled;
    const btn = document.getElementById('tts-btn');
    if (!btn) return;

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

function toggleTTS(forcedState) {
    const nextState = typeof forcedState === 'boolean' ? forcedState : !window.ttsEnabled;
    setTtsUiState(nextState);

    if (window.backend && typeof window.backend.onTtsToggled === 'function') {
        window.backend.onTtsToggled(nextState);
    }
}

function formatVoiceName(voiceId) {
    const names = {
        af_alloy: 'Alloy', af_aoede: 'Aoede', af_bella: 'Bella', af_heart: 'Heart',
        af_jessica: 'Jessica', af_kore: 'Kore', af_nicole: 'Nicole', af_nova: 'Nova',
        af_river: 'River', af_sarah: 'Sarah', af_sky: 'Sky',
        am_adam: 'Adam', am_echo: 'Echo', am_eric: 'Eric', am_fenrir: 'Fenrir',
        am_liam: 'Liam', am_michael: 'Michael', am_onyx: 'Onyx', am_puck: 'Puck', am_santa: 'Santa',
        bf_alice: 'Alice', bf_emma: 'Emma', bf_isabella: 'Isabella', bf_lily: 'Lily',
        bm_daniel: 'Daniel', bm_fable: 'Fable', bm_george: 'George', bm_lewis: 'Lewis',
        ef_dora: 'Dora', em_alex: 'Alex', em_santa: 'Santa', ff_siwis: 'Siwis',
        hf_alpha: 'Alpha', hf_beta: 'Beta', hm_omega: 'Omega', hm_psi: 'Psi',
        if_sara: 'Sara', im_nicola: 'Nicola',
        jf_alpha: 'Alpha', jf_gongitsune: 'Gongitsune', jf_nezumi: 'Nezumi', jf_tebukuro: 'Tebukuro', jm_kumo: 'Kumo',
        pf_dora: 'Dora', pm_alex: 'Alex', pm_santa: 'Santa',
        zf_xiaobei: 'Xiaobei', zf_xiaoni: 'Xiaoni', zf_xiaoxiao: 'Xiaoxiao', zf_xiaoyi: 'Xiaoyi',
        zm_yunjian: 'Yunjian', zm_yunxi: 'Yunxi', zm_yunxia: 'Yunxia', zm_yunyang: 'Yunyang'
    };

    return names[voiceId] ? `${names[voiceId]} (${voiceId})` : voiceId;
}

function groupForVoice(voiceId) {
    if (voiceId.startsWith('af_')) return 'American English • Female';
    if (voiceId.startsWith('am_')) return 'American English • Male';
    if (voiceId.startsWith('bf_')) return 'British English • Female';
    if (voiceId.startsWith('bm_')) return 'British English • Male';
    if (voiceId.startsWith('ef_')) return 'Spanish • Female';
    if (voiceId.startsWith('em_')) return 'Spanish • Male';
    if (voiceId.startsWith('ff_')) return 'French';
    if (voiceId.startsWith('hf_')) return 'Hindi • Female';
    if (voiceId.startsWith('hm_')) return 'Hindi • Male';
    if (voiceId.startsWith('if_')) return 'Italian • Female';
    if (voiceId.startsWith('im_')) return 'Italian • Male';
    if (voiceId.startsWith('jf_')) return 'Japanese • Female';
    if (voiceId.startsWith('jm_')) return 'Japanese • Male';
    if (voiceId.startsWith('pf_')) return 'Portuguese • Female';
    if (voiceId.startsWith('pm_')) return 'Portuguese • Male';
    if (voiceId.startsWith('zf_')) return 'Chinese • Female';
    if (voiceId.startsWith('zm_')) return 'Chinese • Male';
    return 'Other';
}

function populateVoiceSelector(voices) {
    const select = document.getElementById('tts-voice-select');
    if (!select) return;

    const previous = window.ttsVoice;
    const uniqueVoices = [...new Set((voices || []).filter(v => typeof v === 'string' && v.trim()))];
    window.ttsVoices = uniqueVoices;

    select.innerHTML = '';

    const groups = new Map();
    uniqueVoices.forEach((voiceId) => {
        const group = groupForVoice(voiceId);
        if (!groups.has(group)) groups.set(group, []);
        groups.get(group).push(voiceId);
    });

    [...groups.entries()].forEach(([groupName, ids]) => {
        const optgroup = document.createElement('optgroup');
        optgroup.label = groupName;
        ids.sort().forEach((voiceId) => {
            const option = document.createElement('option');
            option.value = voiceId;
            option.textContent = formatVoiceName(voiceId);
            optgroup.appendChild(option);
        });
        select.appendChild(optgroup);
    });

    if (previous && uniqueVoices.includes(previous)) {
        select.value = previous;
    } else if (uniqueVoices.length > 0) {
        select.value = uniqueVoices[0];
        window.ttsVoice = uniqueVoices[0];
    }

    const custom = document.getElementById('tts-voice-custom');
    if (custom) custom.value = '';
}

function applySelectedVoice() {
    const select = document.getElementById('tts-voice-select');
    if (!select || !select.value || !window.backend) return;

    const selected = select.value;
    window.ttsVoice = selected;
    localStorage.setItem('talos.ttsVoice', selected);

    if (typeof window.backend.setTtsVoice === 'function') {
        window.backend.setTtsVoice(selected);
    }
}

function applyCustomVoice() {
    const input = document.getElementById('tts-voice-custom');
    if (!input || !window.backend) return;

    const value = input.value.trim();
    if (!value) return;

    window.ttsVoice = value;
    localStorage.setItem('talos.ttsVoice', value);

    if (typeof window.backend.setTtsVoice === 'function') {
        window.backend.setTtsVoice(value);
    }

    const select = document.getElementById('tts-voice-select');
    if (select) select.value = '';
}

function toggleTtsSettings() {
    const panel = document.getElementById('tts-settings');
    if (panel) panel.classList.toggle('hidden');
}

document.addEventListener("DOMContentLoaded", function () {
    const sendBtn = document.getElementById('send-btn');
    const captureBtn = document.getElementById('capture-btn');
    const micBtn = document.getElementById('mic-btn');
    const ttsBtn = document.getElementById('tts-btn');
    const ttsSettingsBtn = document.getElementById('tts-settings-btn');
    const ttsVoiceApplyBtn = document.getElementById('tts-voice-apply-btn');
    const ttsVoiceCustomBtn = document.getElementById('tts-voice-custom-btn');
    const ttsVoiceCustom = document.getElementById('tts-voice-custom');
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

    sendBtn.addEventListener('click', sendMessage);
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
    ttsSettingsBtn.addEventListener('click', toggleTtsSettings);
    ttsVoiceApplyBtn.addEventListener('click', applySelectedVoice);
    ttsVoiceCustomBtn.addEventListener('click', applyCustomVoice);
    ttsVoiceCustom.addEventListener('keydown', function(e) {
        if (e.key === 'Enter') {
            e.preventDefault();
            applyCustomVoice();
        }
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

            if (typeof window.backend.ttsEnabled !== 'undefined') {
                setTtsUiState(!!window.backend.ttsEnabled);
            }
            if (typeof window.backend.ttsVoice === 'string') {
                window.ttsVoice = window.backend.ttsVoice;
            }
            if (Array.isArray(window.backend.ttsVoices)) {
                populateVoiceSelector(window.backend.ttsVoices);
            }

            if (window.backend.ttsEnabledChanged) {
                window.backend.ttsEnabledChanged.connect(function(enabled) {
                    setTtsUiState(enabled);
                });
            }

            if (window.backend.ttsVoiceChanged) {
                window.backend.ttsVoiceChanged.connect(function(voice) {
                    window.ttsVoice = voice;
                    const select = document.getElementById('tts-voice-select');
                    if (select && [...select.options].some(o => o.value === voice)) {
                        select.value = voice;
                    }
                    localStorage.setItem('talos.ttsVoice', voice);
                });
            }

            if (window.backend.ttsVoicesChanged) {
                window.backend.ttsVoicesChanged.connect(function(voices) {
                    populateVoiceSelector(voices);
                });
            }

            const savedVoice = localStorage.getItem('talos.ttsVoice');
            if (savedVoice && typeof window.backend.setTtsVoice === 'function') {
                window.ttsVoice = savedVoice;
                window.backend.setTtsVoice(savedVoice);
            }
        });
    }
});
