// Structural splitting only. The parser receives inert source, never imports it.
import { readFile, writeFile, mkdir } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { resolve, join } from 'node:path';
import { pathToFileURL } from 'node:url';

const [input, output, parserPath] = process.argv.slice(2);
if (!input || !output || !parserPath) throw new Error('input output Babel-parser-path required');
const { parse } = await import(pathToFileURL(resolve(parserPath)).href);
const bytes = await readFile(input);
const source = bytes.toString('utf8');
const ast = parse(source, { sourceType: 'script' });
const root = ast.program.body[0];
if (root.type !== 'VariableDeclaration' || root.declarations.length !== 1 ||
    root.declarations[0].id.name !== '__webpack_modules__' ||
    root.declarations[0].init.type !== 'ObjectExpression') throw new Error('Unexpected module container');
await mkdir(output, { recursive: false });
const sha = data => createHash('sha256').update(data).digest('hex');
const modules = [];
for (const member of root.declarations[0].init.properties) {
    if (member.type !== 'ObjectMethod' || member.computed || member.key.type !== 'NumericLiteral')
        throw new Error('Unexpected module member');
    const id = String(member.key.value);
    const fragment = source.slice(member.start, member.end);
    const derived = Buffer.from(`self.rspackChunk.push([[0],{${fragment}}]);\n`);
    await writeFile(join(output, `${id}.js`), derived, { flag: 'wx' });
    modules.push({ id, start_utf16: member.start, end_utf16: member.end,
        fragment_sha256: sha(Buffer.from(fragment)), derived_sha256: sha(derived) });
}
await writeFile(join(output, 'modules.json'), JSON.stringify({
    input_sha256: sha(bytes), parser: '@babel/parser',
    transformation: 'Exact AST member span wrapped in inert Rspack registration; never executed', modules
}, null, 2) + '\n', { flag: 'wx' });
console.log(`Split ${modules.length} modules without executing target code`);
