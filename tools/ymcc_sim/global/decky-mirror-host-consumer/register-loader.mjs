import {register} from 'node:module';
register(new URL('./source-loader.mjs',import.meta.url),{data:{config:process.env.YMCC_DECKY_CONSUMER_CONFIG}});
