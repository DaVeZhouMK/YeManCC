import { ref } from 'vue';
export type ProposalVariant = 'a'|'b'|'c';
const requested = new URLSearchParams(location.search).get('variant');
export const proposalVariant=ref<ProposalVariant>(requested==='b'||requested==='c'?requested:'a');
