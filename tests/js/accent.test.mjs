// we-manager AccentFromSchemeColor: the scheme colour SchemeAccent.qml hands
// Plasma as the accent colour.
import { test } from 'node:test';
import assert from 'node:assert/strict';

import {
    parseSchemeColor, userSchemeColor, projectSchemeColor, projectJsonPath,
} from '../../plugin/contents/ui/js/accent.mjs';

test('parseSchemeColor: Wallpaper Engine "r g b" with 0-1 channels', () => {
    assert.deepEqual(parseSchemeColor('0.1 0.5 1'), [0.1, 0.5, 1]);
    assert.deepEqual(parseSchemeColor('  0 0.25   0.75 '), [0, 0.25, 0.75]);
});

test('parseSchemeColor: 0-255 channels are scaled, not clamped to white', () => {
    assert.deepEqual(parseSchemeColor('255 0 51'), [1, 0, 0.2]);
});

test('parseSchemeColor: anything else is no colour', () => {
    for (const v of ['', '0.1 0.2', '0.1 0.2 0.3 0.4', 'red', '#ff0000', '0.1 x 0.3', '-1 0 0', null,
                     undefined, 3, { value: '0 0 0' }])
        assert.equal(parseSchemeColor(v), null, String(v));
});

test('userSchemeColor: the per-wallpaper user value, plain or as {value}', () => {
    assert.deepEqual(userSchemeColor({ schemecolor: '1 0 0' }), [1, 0, 0]);
    assert.deepEqual(userSchemeColor({ schemecolor: { value: '0 1 0' } }), [0, 1, 0]);
    assert.equal(userSchemeColor({}), null);
    assert.equal(userSchemeColor(undefined), null);
    assert.equal(userSchemeColor({ schemecolor: 'oops' }), null);
});

test('projectSchemeColor: general.properties.schemecolor.value of project.json', () => {
    const text = JSON.stringify({ general: { properties: { schemecolor: { type: 'color', value: '0 0 1' } } } });
    assert.deepEqual(projectSchemeColor(text), [0, 0, 1]);
    assert.equal(projectSchemeColor(JSON.stringify({ general: { properties: {} } })), null);
    assert.equal(projectSchemeColor(JSON.stringify({ title: 'x' })), null);
    assert.equal(projectSchemeColor('not json'), null);
    assert.equal(projectSchemeColor(''), null);
});

test('projectJsonPath: next to the wallpaper file, file: URL or native path', () => {
    assert.equal(projectJsonPath('file:///home/u/ws/431960/123/scene.json'), '/home/u/ws/431960/123/project.json');
    assert.equal(projectJsonPath('/home/u/ws/431960/123/index.html'), '/home/u/ws/431960/123/project.json');
    assert.equal(projectJsonPath('file:/v/clip.mp4'), '/v/project.json');
    assert.equal(projectJsonPath(''), '');
    assert.equal(projectJsonPath('scene.json'), '');
});
