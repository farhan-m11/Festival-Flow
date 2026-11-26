const tasks=[], dependencies=[];
const $=id=>document.getElementById(id);
const palette=['#8174e9','#4c9bdc','#e69b52','#42b88b','#d779a8','#6d8ca8'];
function toast(msg){const t=$('toast');t.textContent=msg;t.classList.add('show');setTimeout(()=>t.classList.remove('show'),2300)}
function escapeHtml(s){return String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
function departments(){return [...new Set(tasks.map(t=>t.department))]}
function render(){
 $('taskCount').textContent=tasks.length;$('depCount').textContent=dependencies.length;$('deptCount').textContent=departments().length;$('taskTotal').textContent=`${tasks.length} tasks`;$('groupTotal').textContent=`${departments().length} groups`;
 const list=$('taskList');list.className='task-list'+(!tasks.length?' empty':'');
 list.innerHTML=tasks.length?tasks.map((t,i)=>`<div class="task-chip"><b>${escapeHtml(t.id)}</b><span>${escapeHtml(t.department)}</span><button class="remove" data-task="${i}" title="Remove task">×</button></div>`).join(''):'No tasks yet. Add your first task above.';
 const opts='<option value="">Select task</option>'+tasks.map(t=>`<option value="${escapeHtml(t.id)}">${escapeHtml(t.id)} · ${escapeHtml(t.department)}</option>`).join('');
 $('depFrom').innerHTML=opts;$('depTo').innerHTML=opts;
 $('depList').className='dependency-list'+(!dependencies.length?' empty':'');
 $('depList').innerHTML=dependencies.length?dependencies.map((d,i)=>`<div class="dep-chip">${escapeHtml(d.from)} <span>→</span> ${escapeHtml(d.to)} <button class="remove" data-dep="${i}">×</button></div>`).join(''):'No dependencies added.';
 $('departmentList').innerHTML=departments().length?departments().map((d,i)=>`<div class="dept-row"><i class="dept-color" style="background:${palette[i%palette.length]}"></i><b>${escapeHtml(d)}</b><span>${tasks.filter(t=>t.department===d).length} task(s)</span></div>`).join(''):'<div class="empty-note">Departments appear here as you add tasks.</div>';
}
$('taskForm').addEventListener('submit',e=>{e.preventDefault();const id=$('taskId').value.trim(),department=$('department').value.trim();if(!id||!department)return;if(tasks.some(t=>t.id.toLowerCase()===id.toLowerCase()))return toast('Task ID already exists.');if(departments().length>=15&&!departments().includes(department))return toast('Maximum 15 departments allowed.');if(tasks.length>=300)return toast('Maximum 300 tasks allowed.');tasks.push({id,department});$('taskId').value='';$('department').value='';render();resetResult();});
$('depForm').addEventListener('submit',e=>{e.preventDefault();const from=$('depFrom').value,to=$('depTo').value;if(!from||!to)return;if(from===to)return toast('A task cannot depend on itself.');if(dependencies.some(d=>d.from===from&&d.to===to))return toast('That dependency already exists.');if(dependencies.length>=600)return toast('Maximum 600 dependencies allowed.');dependencies.push({from,to});render();resetResult();});
$('taskList').addEventListener('click',e=>{const b=e.target.closest('[data-task]');if(!b)return;const i=+b.dataset.task,id=tasks[i].id;tasks.splice(i,1);for(let j=dependencies.length-1;j>=0;j--)if(dependencies[j].from===id||dependencies[j].to===id)dependencies.splice(j,1);render();resetResult();});
$('depList').addEventListener('click',e=>{const b=e.target.closest('[data-dep]');if(!b)return;dependencies.splice(+b.dataset.dep,1);render();resetResult();});
function resetResult(){$('statusStat').textContent='Draft';$('resultBadge').className='result-badge neutral';$('resultBadge').textContent='AWAITING INPUT';$('result').className='result-placeholder';$('result').innerHTML='<div class="placeholder-icon">⌁</div><b>Your schedule starts here</b><span>Add tasks and dependencies, then generate your plan.</span>'}
$('clearAll').onclick=()=>{tasks.length=0;dependencies.length=0;render();resetResult();toast('Plan cleared.');};
async function generate(){
 if(!tasks.length)return toast('Add at least one task first.');
 $('generate').disabled=true;$('generate').textContent='Generating…';
 try{
  const res=await fetch('/api/solve',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({tasks,dependencies})});
  const data=await res.json();
  showResult(data);
 }catch(err){toast('Could not reach the C engine. Is the server running?');}
 finally{$('generate').disabled=false;$('generate').innerHTML='Generate schedule <span>→</span>'}
}
$('generate').onclick=generate;
function showResult(data){
 const result=$('result'),badge=$('resultBadge');
 if(data.status==='VALID'){
  $('statusStat').textContent='Valid';$('statusStat').style.color='#2da475';badge.className='result-badge valid';badge.textContent='✓ VALID SCHEDULE';
  const groups=[];for(const item of data.order){let last=groups[groups.length-1];if(!last||last.department!==item.department){last={department:item.department,tasks:[]};groups.push(last)}last.tasks.push(item.task)}
  result.className='';result.innerHTML=`<div class="success-message">✓ ${escapeHtml(data.message)} All ${data.order.length} tasks are scheduled in uninterrupted department blocks.</div><div class="schedule-blocks">${groups.map((g,i)=>`<div class="schedule-dept"><div class="schedule-dept-head"><b><span style="color:${palette[i%palette.length]}">●</span> ${escapeHtml(g.department)}</b><span>BLOCK ${String(i+1).padStart(2,'0')} · ${g.tasks.length} TASK(S)</span></div><div class="schedule-tasks">${g.tasks.map((t,j)=>`<span class="schedule-task">${String(j+1).padStart(2,'0')} &nbsp;${escapeHtml(t)}</span>`).join('')}</div></div>`).join('')}</div>`;
 }else if(data.status==='IMPOSSIBLE'){
  $('statusStat').textContent='Impossible';$('statusStat').style.color='#d66b5f';badge.className='result-badge impossible';badge.textContent='✕ IMPOSSIBLE';
  result.className='';result.innerHTML=`<div class="impossible-message">✕ ${escapeHtml(data.message)}</div><div class="result-placeholder"><div class="placeholder-icon" style="color:#d97869">↻</div><b>No valid grouped order exists</b><span>Change the dependencies or department assignments and try again.</span></div>`;
 }else{badge.className='result-badge impossible';badge.textContent='INPUT ERROR';result.className='';result.innerHTML=`<div class="impossible-message">${escapeHtml(data.message||'Invalid input.')}</div>`}
}
function loadPlan(ts,ds){tasks.splice(0,tasks.length,...ts.map(x=>({...x})));dependencies.splice(0,dependencies.length,...ds.map(x=>({...x})));render();resetResult();generate();document.querySelector('#builder').scrollIntoView({behavior:'smooth'});}
$('loadExample').onclick=()=>loadPlan([{id:'S1',department:'Sound'},{id:'S2',department:'Sound'},{id:'L1',department:'Lights'},{id:'L2',department:'Lights'},{id:'P',department:'Prep'}],[{from:'P',to:'S1'},{from:'S1',to:'S2'},{from:'S2',to:'L1'},{from:'L1',to:'L2'}]);
document.querySelectorAll('[data-test]').forEach(b=>b.addEventListener('click',()=>{
 const t=b.dataset.test;
 if(t==='valid')loadPlan([{id:'S1',department:'Sound'},{id:'S2',department:'Sound'},{id:'L1',department:'Lights'},{id:'L2',department:'Lights'},{id:'P',department:'Prep'}],[{from:'P',to:'S1'},{from:'S1',to:'S2'},{from:'S2',to:'L1'},{from:'L1',to:'L2'}]);
 if(t==='deptcycle')loadPlan([{id:'A1',department:'Audio'},{id:'A2',department:'Audio'},{id:'V1',department:'Visuals'},{id:'V2',department:'Visuals'}],[{from:'A1',to:'V1'},{from:'V2',to:'A2'}]);
 if(t==='taskcycle')loadPlan([{id:'C1',department:'Cultural'},{id:'C2',department:'Cultural'},{id:'F1',department:'Food'}],[{from:'C1',to:'C2'},{from:'C2',to:'C1'}]);
 if(t==='independent')loadPlan([{id:'REG',department:'Registration'},{id:'MED',department:'Medical'},{id:'G1',department:'Games'},{id:'G2',department:'Games'}],[]);
}));
render();
