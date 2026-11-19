// Source-only transform. Applied only by options.msiJointVisibility === true.
export function applyMsiJointVisibilityFake(fake, backendSource) {
 const replace=(before,after)=>{if(fake.split(before).length!==2)throw Error('MSI joint fake anchor drift: '+before);fake=fake.replace(before,after);};
 const generic='private string HandleHidHideCommand(CommandEnvelope e,out bool shutdown){shutdown=false;Fixture.Events.Add(e.Command);return Ack(e,"fake-hidhide");}';
 replace(generic,'private string HandleHidHideCommand(CommandEnvelope e,out bool shutdown){shutdown=false;Fixture.Events.Add(e.Command);var reply=MsiJointVisibilityBackend.Execute(e.Command,e.HidHidePath,e.HidHideCloak,_managedOptions?.Receipt,e.RunId,e.Epoch,e.PowerGeneration,_phase.ToString());return reply.Success?Ack(e,reply.Status,reply.Detail):Error(e,reply.Status,reply.Detail);}');
 replace('private string HandleCloseRefreshBegin(CommandEnvelope e){Fixture.Events.Add(e.Command);return Ack(e,"fake-cycle-begin");}','private string HandleCloseRefreshBegin(CommandEnvelope e){return HandleHidHideCommand(e,out _);}');
 replace('private string HandleCloseRefreshPoll(CommandEnvelope e){Fixture.Events.Add(e.Command);return Ack(e,"fake-cycle-poll");}','private string HandleCloseRefreshPoll(CommandEnvelope e){return HandleHidHideCommand(e,out _);}');
 if(typeof backendSource!=='string'||!backendSource.includes('private static class MsiJointVisibilityBackend'))throw Error('MSI joint backend template missing');
 return {fake,backendSource};
}
