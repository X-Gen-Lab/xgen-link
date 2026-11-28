# SPDX-License-Identifier: MIT
# Adapted from Nexus acbc46e1, tools/documentation/search_language.py.
#
# MIT License
#
# Copyright (c) 2026 Nexus Embedded Platform Team
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#
"""Dictionary-free Han discovery through Sphinx's public search language API.

Adjacent bigrams and individual Han characters do not cross punctuation or Latin
identifiers. Search retains Sphinx's bag-of-terms ranking and AND behavior; this
is not positional phrase search. Internal long keys avoid Sphinx's optional
short-term fallback while the query, highlights and labels keep original text.
"""
import re

from sphinx.search.en import SearchEnglish


# Basic/Extension A, compatibility, supplementary B-F/I and G-H ideographs.
_HAN = (r'[\u3400-\u4dbf\u4e00-\u9fff\uf900-\ufaff'
        r'\U00020000-\U0002fa1f\U00030000-\U000323af]')
_HAN_RUN = re.compile(_HAN + '+')
_PARTS = re.compile('(' + _HAN + '+)')
_WORDS = re.compile('(?:' + _HAN + r'|\w)+')
_QUERY_WORDS = re.compile(r'(?<!(?:' + _HAN + r'|\w))-?(?:' + _HAN + r'|\w)+')
_PREFIX = 'xglcjk_'
_JS_HAN = (r'[\u3400-\u4dbf\u4e00-\u9fff\uf900-\ufaff'
           r'\u{20000}-\u{2fa1f}\u{30000}-\u{323af}]')


def split_terms(text, *, query=False):
    """Return bounded, deduplicated raw tokens using Unicode codepoints."""
    terms = []
    for word in (_QUERY_WORDS if query else _WORDS).findall(text):
        sign = '-' if query and word.startswith('-') else ''
        word = word[1:] if sign else word
        for part in _PARTS.split(word):
            if not part:
                continue
            if _HAN_RUN.fullmatch(part):
                if not query or len(part) == 1:
                    terms.extend(sign + char for char in part)
                terms.extend(sign + part[i:i + 2] for i in range(len(part) - 1))
            else:
                terms.append(sign + part)
    return list(dict.fromkeys(terms))


class XglChineseSearch(SearchEnglish):
    """Keep Porter Latin behavior and add deterministic zh_CN Han discovery."""
    lang = 'zh_CN'
    language_name = 'Chinese'
    js_splitter_code = r'''
var splitQuery = function(query) {
    const parts = /(''' + _JS_HAN + r'''+)/u;
    const han = /^''' + _JS_HAN + r'''+$/u;
    const terms = [];
    // A hyphen inside a word remains a separator; a leading minus at a word
    // boundary retains Sphinx's exclusion syntax, including supplementary Han.
    const words = /(?<!(?:''' + _JS_HAN + r'''|[\p{Letter}\p{Number}_]))-?(?:''' + _JS_HAN + r'''|[\p{Letter}\p{Number}_])+/gu;
    for (const match of query.matchAll(words)) {
        const sign = match[0].startsWith('-') ? '-' : '';
        const word = sign ? match[0].slice(1) : match[0];
        for (const part of word.split(parts).filter(Boolean)) {
            if (han.test(part)) {
                const chars = Array.from(part);
                if (chars.length === 1) {
                    terms.push(sign + chars[0]);
                }
                for (let i = 0; i + 1 < chars.length; i++) {
                    terms.push(sign + chars[i] + chars[i + 1]);
                }
            } else {
                terms.push(sign + part);
            }
        }
    }
    return [...new Set(terms)];
};
'''
    js_stemmer_code = SearchEnglish.js_stemmer_code + r'''
var XglLatinStemmer = Stemmer;
var Stemmer = function() {
    const latin = new XglLatinStemmer();
    const han = /^''' + _JS_HAN + r'''+$/u;
    this.stemWord = function(word) {
        const sign = word.startsWith('-') ? '-' : '';
        const term = sign ? word.slice(1) : word;
        return sign + (han.test(term) ? 'xglcjk_' + term : latin.stemWord(term));
    };
};
'''

    def split(self, text):
        return split_terms(text)

    def stem(self, word):
        sign = '-' if word.startswith('-') else ''
        term = word[1:] if sign else word
        return sign + (_PREFIX + term if _HAN_RUN.fullmatch(term)
                       else super().stem(term))

    def word_filter(self, word):
        return len(word) > 1 and super().word_filter(word)
