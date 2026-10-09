using HandheldCompanion.Extensions;
using HandheldCompanion.Shared;
using HandheldCompanion.Utils;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Management;
using System.Threading.Tasks;

namespace HandheldCompanion
{
    public static class WMI
    {
        public static async Task<bool> ExistsAsync(string scope, FormattableString query)
        {
            try
            {
                var queryFormatted = query.ToString(WMIPropertyValueFormatter.Instance);
                var mos = new ManagementObjectSearcher(scope, queryFormatted);
                var managementObjects = await mos.GetAsync().ConfigureAwait(false);
                return managementObjects.Any();
            }
            catch
            {
                return false;
            }
        }

        public static IDisposable Listen(string scope, FormattableString query, Action<PropertyDataCollection> handler)
        {
            var queryFormatted = query.ToString(WMIPropertyValueFormatter.Instance);
            var watcher = new ManagementEventWatcher(scope, queryFormatted);
            watcher.EventArrived += (_, e) => handler(e.NewEvent.Properties);
            watcher.Start();

            return new LambdaDisposable(() =>
            {
                watcher.Stop();
                watcher.Dispose();
            });
        }

        public static async Task<IEnumerable<T>> ReadAsync<T>(string scope, FormattableString query, Func<PropertyDataCollection, T> converter)
        {
            try
            {
                var queryFormatted = query.ToString(WMIPropertyValueFormatter.Instance);
                var mos = new ManagementObjectSearcher(scope, queryFormatted);
                var managementObjects = await mos.GetAsync().ConfigureAwait(false);
                var result = managementObjects.Select(mo => mo.Properties).Select(converter);
                return result;
            }
            catch (ManagementException ex)
            {
                LogManager.LogError($"Read failed: {ex.Message}. [scope={scope}, query={query}]", ex);
                return Enumerable.Empty<T>();
            }
        }

        public static int GetAPLength(byte iDataBlockIndex)
        {
            /*
             * Get_AP
             * switch (iDataBlockIndex)
             * case 0: length = 6;
             * case 1: length = 3;
             * case 2: length = 7;
             */
            switch (iDataBlockIndex)
            {
                case 0:
                    return 6;
                case 1:
                    return 3;
                case 2:
                    return 7;
                default:
                    return 32;
            }
        }

        public static ManagementBaseObject? Set(string scope, string path, string methodName, byte[] fullPackage)
        {
            // Create the management object using the provided scope and path
            ManagementObject managementObject = new ManagementObject(scope, path, null);

            ManagementBaseObject? inParams = null;
            ManagementBaseObject? inParamsData = null;
            bool parametersAvailable = false;

            // First attempt: retrieve method parameters for specified methodName
            try
            {
                inParams = managementObject.GetMethodParameters(methodName);
                inParamsData = inParams["Data"] as ManagementBaseObject;
                parametersAvailable = (inParams != null && inParamsData != null);
            }
            catch (Exception) { }

            // If the "Data" parameter was not obtained, try the fallback method "Get_WMI"
            if (!parametersAvailable)
            {
                try
                {
                    inParams = managementObject.InvokeMethod("Get_WMI", null, null);
                    inParamsData = inParams["Data"] as ManagementBaseObject;
                }
                catch (ManagementException) { }
                catch (Exception) { }
            }

            // If we still don't have valid input parameters, throw an exception
            if (inParams == null || inParamsData == null)
            {
                LogManager.LogError("WMI Call failed: [scope={0}, path={1}, methodName={2}, fullPackage={3}]", scope, path, methodName, string.Join(',', fullPackage));
                return null;
            }

            // Set the "Bytes" property of the "Data" parameter to the full package
            inParamsData.SetPropertyValue("Bytes", fullPackage);
            inParams.SetPropertyValue("Data", inParamsData);

            // Invoke the method with the parameters
            return managementObject.InvokeMethod(methodName, inParams, null);
        }

        public static byte[] Get(string scope, string path, string methodName, byte iDataBlockIndex, int length, out bool readSuccess)
        {
            byte[] fullPackage = new byte[32];
            byte[] resultData = new byte[length];

            fullPackage[0] = iDataBlockIndex;
            readSuccess = false;

            // Extract the output bytes from the nested 'Data' object.
            ManagementBaseObject? outParams = Set(scope, path, methodName, fullPackage);
            if (outParams == null)
                return resultData;

            ManagementBaseObject? dataOut = outParams["Data"] as ManagementBaseObject;
            if (dataOut == null)
            {
                LogManager.LogError("WMI Call failed at outParams[\"Data\"]: [scope={0}, path={1}, methodName={2}, iDataBlockIndex={3}, length={4}]", scope, path, methodName, iDataBlockIndex, length);
                return resultData;
            }

            byte[]? outBytes = dataOut["Bytes"] as byte[];
            if (outBytes == null || outBytes.Length < 1)
            {
                LogManager.LogError("WMI Call failed at dataOut[\"Bytes\"]: [scope={0}, path={1}, methodName={2}, iDataBlockIndex={3}, length={4}]", scope, path, methodName, iDataBlockIndex, length);
                return resultData;
            }

            // The first byte is the flag; subsequent bytes contain the actual data.
            byte flag = outBytes[0];
            readSuccess = (flag == 1);

            // Copy the remaining bytes as the returned data.
            int dataLength = outBytes.Length - 1;
            resultData = new byte[dataLength];
            Array.Copy(outBytes, 1, resultData, 0, dataLength);

            return resultData;
        }

        // FAN-936 -----------------------------------------------------------------
        // Structured, non-throwing counterparts of Set()/Get().
        // The global Set()/Get() signatures and semantics are intentionally left
        // untouched: these helpers only ADD the ability to tell apart
        //   transport failure / missing parameters / missing data block / null bytes
        //   / short payload / device flag failure / exception
        // so that "the WMI call returned" can never be reported as "the hardware
        // was restored". Only an outcome of Ok is treated as an effective read.

        public enum WmiIoOutcome
        {
            Ok = 0,
            FlagNotSuccessful = 1,
            ShortPayload = 2,
            EmptyPayload = 3,
            DataBlockMissing = 4,
            BytesMissing = 5,
            InParamsUnavailable = 6,
            TransportFailed = 7,
            Exception = 8,

            // The operation was never attempted. Used by the MSI result objects
            // so that an unused field is never read back as a round trip that
            // succeeded.
            NotAttempted = -1,
        }

        public sealed class WmiIoResult
        {
            public string Operation { get; set; } = string.Empty;
            public string MethodName { get; set; } = string.Empty;
            public byte DataBlockIndex { get; set; }

            // Minimal number of payload bytes required for the caller to proceed.
            public int ExpectedLength { get; set; }

            public WmiIoOutcome Outcome { get; set; } = WmiIoOutcome.Exception;
            public byte Flag { get; set; }
            public byte[] Data { get; set; } = Array.Empty<byte>();
            public string Detail { get; set; } = string.Empty;

            // The transport reached the WMI method and came back with an object.
            public bool TransportOk =>
                Outcome != WmiIoOutcome.TransportFailed &&
                Outcome != WmiIoOutcome.InParamsUnavailable &&
                Outcome != WmiIoOutcome.Exception;

            // The device-level flag byte reported success.
            public bool DeviceFlagOk =>
                Outcome == WmiIoOutcome.Ok ||
                Outcome == WmiIoOutcome.ShortPayload ||
                Outcome == WmiIoOutcome.EmptyPayload;

            // Strict: complete, flag-successful payload.
            public bool IsUsable => Outcome == WmiIoOutcome.Ok;

            public int PayloadLength => Data.Length;

            public bool TryGetFirstByte(out byte value)
            {
                if (Data.Length > 0)
                {
                    value = Data[0];
                    return true;
                }

                value = 0;
                return false;
            }
        }

        public static WmiIoResult GetStructured(string scope, string path, string methodName, byte iDataBlockIndex, int length, int minimumLength = -1)
        {
            WmiIoResult result = new()
            {
                Operation = "Get",
                MethodName = methodName,
                DataBlockIndex = iDataBlockIndex,
                ExpectedLength = minimumLength >= 0 ? minimumLength : length,
            };

            try
            {
                byte[] fullPackage = new byte[32];
                fullPackage[0] = iDataBlockIndex;

                ManagementBaseObject? outParams = Set(scope, path, methodName, fullPackage);
                if (outParams == null)
                {
                    // Set() returns null either when the input parameters could not be
                    // obtained or when InvokeMethod itself returned null. Both mean the
                    // read never produced a device answer.
                    result.Outcome = WmiIoOutcome.TransportFailed;
                    result.Detail = "Set() returned null (no input parameters or null method result)";
                    return result;
                }

                ManagementBaseObject? dataOut = outParams["Data"] as ManagementBaseObject;
                if (dataOut == null)
                {
                    result.Outcome = WmiIoOutcome.DataBlockMissing;
                    result.Detail = "outParams[Data]=null";
                    return result;
                }

                byte[]? outBytes = dataOut["Bytes"] as byte[];
                if (outBytes == null || outBytes.Length < 1)
                {
                    result.Outcome = WmiIoOutcome.BytesMissing;
                    result.Detail = outBytes == null ? "dataOut[Bytes]=null" : "dataOut[Bytes] empty";
                    return result;
                }

                result.Flag = outBytes[0];

                int dataLength = outBytes.Length - 1;
                byte[] payload = new byte[dataLength];
                if (dataLength > 0)
                    Array.Copy(outBytes, 1, payload, 0, dataLength);
                result.Data = payload;

                if (result.Flag != 1)
                {
                    result.Outcome = WmiIoOutcome.FlagNotSuccessful;
                    result.Detail = $"flag={result.Flag}";
                    return result;
                }

                if (dataLength < 1)
                {
                    result.Outcome = WmiIoOutcome.EmptyPayload;
                    result.Detail = "flag=1 but payload is empty";
                    return result;
                }

                if (dataLength < result.ExpectedLength)
                {
                    result.Outcome = WmiIoOutcome.ShortPayload;
                    result.Detail = $"payload={dataLength}, expected>={result.ExpectedLength}";
                    return result;
                }

                result.Outcome = WmiIoOutcome.Ok;
                return result;
            }
            catch (Exception ex)
            {
                result.Outcome = WmiIoOutcome.Exception;
                result.Detail = $"exception: {ex.Message}";
                return result;
            }
        }

        public static WmiIoResult SetStructured(string scope, string path, string methodName, byte[] fullPackage)
        {
            WmiIoResult result = new()
            {
                Operation = "Set",
                MethodName = methodName,
                DataBlockIndex = fullPackage.Length > 0 ? fullPackage[0] : (byte)0,
                ExpectedLength = 0,
                Data = Array.Empty<byte>(),
            };

            try
            {
                ManagementObject managementObject = new(scope, path, null);

                ManagementBaseObject? inParams = null;
                ManagementBaseObject? inParamsData = null;
                bool parametersAvailable = false;

                try
                {
                    inParams = managementObject.GetMethodParameters(methodName);
                    inParamsData = inParams["Data"] as ManagementBaseObject;
                    parametersAvailable = (inParams != null && inParamsData != null);
                }
                catch (Exception ex)
                {
                    result.Detail = $"GetMethodParameters: {ex.Message}";
                }

                if (!parametersAvailable)
                {
                    try
                    {
                        inParams = managementObject.InvokeMethod("Get_WMI", null, null);
                        inParamsData = inParams["Data"] as ManagementBaseObject;
                        parametersAvailable = (inParams != null && inParamsData != null);

                        if (parametersAvailable)
                            result.Detail = "fallback Get_WMI";
                    }
                    catch (Exception ex)
                    {
                        result.Detail = $"Get_WMI fallback: {ex.Message}";
                    }
                }

                if (inParams == null || inParamsData == null)
                {
                    result.Outcome = WmiIoOutcome.InParamsUnavailable;
                    result.Detail = string.IsNullOrEmpty(result.Detail) ? "inParams/inParamsData unavailable" : result.Detail;
                    LogManager.LogError("WMI SetStructured failed: [scope={0}, path={1}, methodName={2}, fullPackage={3}]", scope, path, methodName, string.Join(',', fullPackage));
                    return result;
                }

                inParamsData.SetPropertyValue("Bytes", fullPackage);
                inParams.SetPropertyValue("Data", inParamsData);

                ManagementBaseObject? outParams = managementObject.InvokeMethod(methodName, inParams, null);
                if (outParams == null)
                {
                    result.Outcome = WmiIoOutcome.TransportFailed;
                    result.Detail = "InvokeMethod returned null";
                    return result;
                }

                result.Outcome = WmiIoOutcome.Ok;
                return result;
            }
            catch (Exception ex)
            {
                result.Outcome = WmiIoOutcome.Exception;
                result.Detail = $"exception: {ex.Message}";
                return result;
            }
        }

        public static void Call(string scope, string query, string methodName, Dictionary<string, object> methodParams)
        {
            using var searcher = new ManagementObjectSearcher(scope, query);
            var managementObject = searcher.Get().Cast<ManagementObject>().FirstOrDefault();

            if (managementObject == null)
                return;

            using var methodParamsObject = managementObject.GetMethodParameters(methodName);
            foreach (var pair in methodParams)
                methodParamsObject[pair.Key] = pair.Value;

            managementObject.InvokeMethod(methodName, methodParamsObject, null);
        }

        public static T? Call<T>(string scope, string query, string methodName, Dictionary<string, object> methodParams, Func<PropertyDataCollection, T> resultSelector)
        {
            using var searcher = new ManagementObjectSearcher(scope, query);
            var managementObject = searcher.Get().Cast<ManagementObject>().FirstOrDefault();

            if (managementObject == null)
                return default;

            using var methodParamsObject = managementObject.GetMethodParameters(methodName);
            foreach (var pair in methodParams)
                methodParamsObject[pair.Key] = pair.Value;

            var result = managementObject.InvokeMethod(methodName, methodParamsObject, null);
            return resultSelector(result.Properties);
        }

        public static async Task CallAsync(string scope, FormattableString query, string methodName, Dictionary<string, object> methodParams)
        {
            try
            {
                var queryFormatted = query.ToString(WMIPropertyValueFormatter.Instance);
                var mos = new ManagementObjectSearcher(scope, queryFormatted);
                var managementObjects = await mos.GetAsync().ConfigureAwait(false);
                var managementObject = managementObjects.FirstOrDefault();

                // Check if managementObject is null and return the default value
                if (managementObject == null)
                    return;

                var mo = (ManagementObject)managementObject;
                var methodParamsObject = mo.GetMethodParameters(methodName);
                foreach (var pair in methodParams)
                    methodParamsObject[pair.Key] = pair.Value;

                mo.InvokeMethod(methodName, methodParamsObject, new InvokeMethodOptions());
            }
            catch (ManagementException ex)
            {
                LogManager.LogError($"Call failed: {ex.Message}. [scope={scope}, query={query}, methodName={methodName}]", ex);
            }
        }

        public static async Task<T?> CallAsync<T>(string scope, FormattableString query, string methodName, Dictionary<string, object> methodParams, Func<PropertyDataCollection, T> converter)
        {
            try
            {
                var queryFormatted = query.ToString(WMIPropertyValueFormatter.Instance);

                var mos = new ManagementObjectSearcher(scope, queryFormatted);
                var managementObjects = await mos.GetAsync().ConfigureAwait(false);
                var managementObject = managementObjects.FirstOrDefault();

                // Check if managementObject is null and return the default value
                if (managementObject == null)
                    return default;

                var mo = (ManagementObject)managementObject;
                var methodParamsObject = mo.GetMethodParameters(methodName);
                foreach (var pair in methodParams)
                    methodParamsObject[pair.Key] = pair.Value;

                var resultProperties = mo.InvokeMethod(methodName, methodParamsObject, new InvokeMethodOptions());
                var result = converter(resultProperties.Properties);
                return result;
            }
            catch (ManagementException ex)
            {
                // Log the exception details and return the default value
                LogManager.LogError($"Call failed: {ex.Message}. [scope={scope}, query={query}, methodName={methodName}]");
                return default;
            }
        }

        public class WMIPropertyValueFormatter : IFormatProvider, ICustomFormatter
        {
            public static readonly WMIPropertyValueFormatter Instance = new();

            private WMIPropertyValueFormatter() { }

            public object GetFormat(Type? formatType)
            {
                if (formatType == typeof(ICustomFormatter))
                    return this;

                throw new InvalidOperationException("Invalid type of formatted");
            }

            public string Format(string? format, object? arg, IFormatProvider? formatProvider)
            {
                var stringArg = arg?.ToString()?.Replace("\\", "\\\\");
                return stringArg ?? string.Empty;
            }
        }
    }
}
