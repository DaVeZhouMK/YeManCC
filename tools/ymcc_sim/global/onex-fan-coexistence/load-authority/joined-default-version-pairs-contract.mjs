export {createSchema6JoinedDefaultModel,schema6ProviderCatalog,validateRequestBindingModel} from './exact-framework-version-pairs-contract.mjs';
import {validateAliasPolicyModel} from './exact-framework-version-pairs-contract.mjs';
// Independent model entry for the joined-only profile. It shares the existing
// exact provider predicate and never changes a three-scope schema5 policy.
export function validateJoinedDefaultPolicyModel(policy){
 if(policy?.schemaVersion!==6)throw new Error('EXPECTED_SCHEMA6_JOINED_DEFAULT');
 return validateAliasPolicyModel(policy);
}
