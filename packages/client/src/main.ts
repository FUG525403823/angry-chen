import { formatVersionLine } from './index.ts';

const app = document.querySelector('#app');
if (app === null) {
  throw new Error('缺少 #app 挂载点');
}
app.textContent = formatVersionLine();
