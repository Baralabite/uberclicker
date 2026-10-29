import {execFileSync} from 'node:child_process';
import {readFileSync,readdirSync,writeFileSync,mkdirSync} from 'node:fs';
import path from 'node:path';
const root=process.cwd();
const dependencies=execFileSync(process.platform==='win32'?'npm.cmd':'npm',['ls','--omit=dev','--parseable'],{encoding:'utf8'}).trim().split('\n').filter(p=>path.resolve(p)!==root);
const notices=['UberClicker dependency notices\nProject: MIT (see LICENSE). Dependency licenses follow.\n'];
for(const directory of dependencies.sort()){
 const meta=JSON.parse(readFileSync(path.join(directory,'package.json'),'utf8'));
 notices.push(`\n${'='.repeat(72)}\n${meta.name} ${meta.version}\nDeclared license: ${meta.license??'See upstream'}\n`);
 for(const file of readdirSync(directory).sort())if(/^(license|notice|copying)(\.|$)/i.test(file)){
  notices.push(file+'\n'+readFileSync(path.join(directory,file),'utf8')+'\n');
 }
}
notices.push('\n'+readFileSync('public/native-notices.txt','utf8'));
mkdirSync('public',{recursive:true});writeFileSync('public/third-party-notices.txt',notices.join('\n'));
