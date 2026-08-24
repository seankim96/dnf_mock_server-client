using System.IO;
using System.Net;
using System.Net.Security;
using System.Net.Sockets;
using System.Security.Authentication;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using Dnf.Protocol;
using DnfMockClient.Networking;
using DnfMockClient.Protocol;
using Google.FlatBuffers;
using Xunit;
using static DnfMockClient.Tests.TestAssertions;
using static DnfMockClient.Tests.TestPayloads;
using AuthSchema = Dnf.Protocol.Auth;
using TcpSchema = Dnf.Protocol.Tcp;

namespace DnfMockClient.Tests;

public sealed class CertificateFingerprintTests
{
    [Fact]
    public void ValidatesConfiguredCertificateFingerprints()
    {
        Assert(CertificateFingerprint.CreateValidation(string.Empty) is null,
            "An empty certificate fingerprint must use system validation.");

        RemoteCertificateValidationCallback? validation =
            CertificateFingerprint.CreateValidation(new string('A', 64));
        Assert(validation is not null &&
            !validation(new object(), null, null, SslPolicyErrors.None),
            "Fingerprint validation accepted a missing certificate.");

        AssertThrows<ArgumentException>(
            () => CertificateFingerprint.CreateValidation("ABCD"),
            "A short certificate fingerprint was accepted.");
        AssertThrows<ArgumentException>(
            () => CertificateFingerprint.CreateValidation(new string('Z', 64)),
            "A non-hexadecimal certificate fingerprint was accepted.");
    }

}
