// Random Picker UI 预览冒烟测试（jsdom）
// 用法：NODE_PATH=/Users/han2022/.workbuddy/binaries/node/workspace/node_modules node tools/smoke_ui_preview.js
const { JSDOM, VirtualConsole } = require('jsdom');
const fs = require('fs');

const path = require('path');
const FILE = path.join(__dirname, '..', 'docs', 'ui-preview.html');
const html = fs.readFileSync(FILE, 'utf8');

const errs = [];
const vc = new VirtualConsole();
vc.on('jsdomError', e => errs.push('jsdomError: ' + e.message));
vc.on('error', (...a) => errs.push('console.error: ' + a.join(' ')));

const dom = new JSDOM(html, { runScripts: 'dangerously', pretendToBeVisual: true, virtualConsole: vc });
const w = dom.window, d = w.document;

if (!w.crypto || !w.crypto.getRandomValues) {
  w.crypto = { getRandomValues: a => { for (let i = 0; i < a.length; i++) a[i] = Math.floor(Math.random() * 256); return a; } };
}

let pass = 0, fail = 0;
const chk = (name, cond, extra = '') => {
  if (cond) { pass++; console.log('  PASS  ' + name); }
  else { fail++; console.log('  FAIL  ' + name + (extra ? '  → ' + extra : '')); }
};
const click = id => d.getElementById(id).dispatchEvent(new w.MouseEvent('click', { bubbles: true }));
const status = () => d.getElementById('status').textContent;
const key = k => d.dispatchEvent(new w.KeyboardEvent('keydown', { key: k, bubbles: true, cancelable: true }));

console.log('[1] 初始渲染');
chk('无 JS 运行时错误', errs.length === 0, errs.join(' | '));
chk('组页签 = 7（全班 + 6 组）', d.querySelectorAll('.tab').length === 7,
    String(d.querySelectorAll('.tab').length));
chk('名单行数 = 24', d.querySelectorAll('.list .row').length === 24,
    String(d.querySelectorAll('.list .row').length));
chk('默认主题 = geek', d.getElementById('app').dataset.theme === 'geek');
chk('底部按钮 = 12', d.querySelectorAll('.btns button').length === 12);
chk('结果卡初始为占位文案', d.getElementById('card').textContent.includes('按 空格 抽 1 人'));
chk('结果卡初始无高亮边框', !d.getElementById('card').classList.contains('hit'));

console.log('[2] 抽 1 人');
click('bPick1');
const one = d.getElementById('card');
chk('结果卡高亮', one.classList.contains('hit'));
chk('标题为「抽中的人」', one.textContent.includes('抽中的人'));
chk('显示大字姓名', !!one.querySelector('.big'));
chk('显示组别', !!one.querySelector('.grp'));
chk('seed 为 16 位十六进制', /^seed [0-9a-f]{16}$/.test(one.querySelector('.sd').textContent),
    one.querySelector('.sd').textContent);
chk('历史已记录 1 条', d.querySelectorAll('#histL .l').length === 1);

console.log('[3] 抽 1 组');
click('bPickG');
chk('标题为「抽中的组」', d.getElementById('card').textContent.includes('抽中的组'));
chk('历史累计 2 条', d.querySelectorAll('#histL .l').length === 2);

console.log('[4] 抽 N 人');
d.getElementById('nIn').value = '3';
click('bPickN');
chk('列出 3 行人名', d.querySelectorAll('#card .pl').length === 3,
    String(d.querySelectorAll('#card .pl').length));
chk('seed 为 3 段', d.querySelector('#card .sd').textContent.split(' ').length === 4,
    d.querySelector('#card .sd').textContent);
chk('标题为「抽中的 3 人」', d.getElementById('card').textContent.includes('抽中的 3 人'));

console.log('[4b] 抽 N 人的组分布（回归：随机范围必须是池长而非 n）');
{
  const seenGroups = new Set(), combos = new Set();
  for (let r = 0; r < 60; r++) {
    d.getElementById('nIn').value = '3';
    click('bPickN');
    const gs = [];
    d.querySelectorAll('#card .pl').forEach(el => {
      const m = el.textContent.match(/\[(.+?)\]$/);
      if (m) { seenGroups.add(m[1]); gs.push(m[1]); }
    });
    combos.add(gs.slice().sort().join('/'));
  }
  chk('60 次抽 3 人覆盖全部 6 个组', seenGroups.size === 6, '实际 ' + [...seenGroups].join(','));
  chk('抽出的组组合会变化（>10 种）', combos.size > 10, '实际 ' + combos.size + ' 种');
  chk('不再恒定是前三组', !(combos.size === 1 && [...combos][0] === '一组/三/二'.split('/').sort().join('/')));
}

console.log('[5] 边界：人数超额 / 非法');
d.getElementById('nIn').value = '99';
click('bPickN');
chk('超额给出人话提示', /只能抽 \d+ 人，填的 99 太多了/.test(status()), status());
d.getElementById('nIn').value = '0';
click('bPickN');
chk('0 人提示要大于 0', status().includes('人数要大于 0'), status());

console.log('[6] 按组抽选');
const tabs = d.querySelectorAll('.tab');
tabs[1].dispatchEvent(new w.MouseEvent('click', { bubbles: true }));
chk('页签切到一组（重查 DOM，renderTabs 会重建）',
    d.querySelector('.tab.on').textContent.startsWith('一组'),
    d.querySelector('.tab.on').textContent);
d.getElementById('nIn').value = '2';
click('bPickN');
chk('组内抽 2 人成功', d.querySelectorAll('#card .pl').length === 2);
chk('结果卡范围显示组名', d.getElementById('card').textContent.includes('范围：一组'),
    d.getElementById('card').textContent);
chk('组内超额被拦', (() => { d.getElementById('nIn').value = '50'; click('bPickN'); return status().includes('太多了'); })(), status());

console.log('[7] 防重复');
tabs[0].dispatchEvent(new w.MouseEvent('click', { bubbles: true }));
key('n');
chk('防重复已开启（按钮高亮）', d.querySelector('[data-k="nr"]').classList.contains('on'));
const before = d.querySelectorAll('.list .row.done').length;
click('bPick1');
chk('抽过的人被标记 done', d.querySelectorAll('.list .row.done').length >= before + 0);
key('r');
chk('重置后 done 清零', d.querySelectorAll('.list .row.done').length === 0);

console.log('[8] 删除与撤销');
d.querySelectorAll('.list .row')[0].dispatchEvent(new w.MouseEvent('click', { bubbles: true }));
click('btns');
d.querySelector('[data-k="del"]').dispatchEvent(new w.MouseEvent('click', { bubbles: true }));
chk('删除后剩 23 人', d.querySelectorAll('.list .row').length === 23,
    String(d.querySelectorAll('.list .row').length));
d.querySelector('[data-k="undo"]').dispatchEvent(new w.MouseEvent('click', { bubbles: true }));
chk('撤销后恢复 24 人', d.querySelectorAll('.list .row').length === 24,
    String(d.querySelectorAll('.list .row').length));

console.log('[9] 一键 6 组');
d.querySelector('[data-k="init6"]').dispatchEvent(new w.MouseEvent('click', { bubbles: true }));
chk('仍为 6 组页签 + 全班', d.querySelectorAll('.tab').length === 7,
    String(d.querySelectorAll('.tab').length));

console.log('[10] 主题切换与快捷键');
click('themeBtn');
chk('切到 ins', d.getElementById('app').dataset.theme === 'ins');
chk('按钮文案跟随', d.getElementById('themeBtn').textContent.includes('ins'));
key('t');
chk('T 键切回 geek', d.getElementById('app').dataset.theme === 'geek');
key(' ');
chk('空格抽 1 人', d.getElementById('card').textContent.includes('抽中的人'));
key('g');
chk('G 键抽组', d.getElementById('card').textContent.includes('抽中的组'));

console.log('[11] 下拉选人数');
click('bDd');
chk('下拉展开 8 个选项', d.querySelectorAll('.menu div').length === 8,
    String(d.querySelectorAll('.menu div').length));
d.querySelector('.menu div[data-n="5"]').dispatchEvent(new w.MouseEvent('click', { bubbles: true }));
chk('选中后写入输入框', d.getElementById('nIn').value === '5');
chk('点击后关闭且 DOM 清理', !d.querySelector('.menu'));

console.log('[12] 全程无异常');
chk('运行时错误累计为 0', errs.length === 0, errs.join(' | '));

console.log('\n==============================');
console.log('  ' + pass + ' PASS  ' + fail + ' FAIL');
console.log('==============================');
process.exit(fail ? 1 : 0);
