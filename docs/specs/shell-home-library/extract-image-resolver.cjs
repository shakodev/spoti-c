const fs=require("node:fs"),crypto=require("node:crypto"),path=require("node:path");
const parser=require(process.argv[3]),source=fs.readFileSync(process.argv[2],"utf8");
const ast=parser.parse(source,{sourceType:"script"}),root=ast.program.body[0];
if(root.type!=="VariableDeclaration"||root.declarations[0].id.name!=="__webpack_modules__")throw Error("container");
const wanted=new Set(process.argv.slice(5)),output=process.argv[4],modules=[];
fs.mkdirSync(output,{recursive:false});
const hash=s=>crypto.createHash("sha256").update(s).digest("hex");
for(const member of root.declarations[0].init.properties){
  const id=String(member.key.value);if(!wanted.has(id))continue;
  const fragment=source.slice(member.start,member.end),bytes=`self.rspackChunk.push([[0],{${fragment}}]);\n`;
  fs.writeFileSync(path.join(output,`${id}.js`),bytes,{flag:"wx"});
  modules.push({id,start_utf16:member.start,end_utf16:member.end,fragment_sha256:hash(fragment),derived_sha256:hash(bytes)});
}
fs.writeFileSync(path.join(output,"extraction.json"),JSON.stringify({input_sha256:hash(source),transformation:
  "Exact AST member span wrapped in inert Rspack registration; target code never executed",modules},null,2));
console.log(JSON.stringify({modules}));
