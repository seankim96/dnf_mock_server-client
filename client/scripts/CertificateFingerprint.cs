using System;
using System.Net.Security;
using System.Security.Cryptography;

namespace DnfMockClient.Networking;

public static class CertificateFingerprint
{
    public static RemoteCertificateValidationCallback? CreateValidation(
        string fingerprint)
    {
        string expectedFingerprint = fingerprint
            .Replace(":", string.Empty)
            .Replace(" ", string.Empty)
            .Trim();

        if (expectedFingerprint.Length == 0)
        {
            return null;
        }

        if (expectedFingerprint.Length != 64)
        {
            throw new ArgumentException(
                "SHA-256 인증서 지문은 64자리 16진수여야 합니다.");
        }

        foreach (char character in expectedFingerprint)
        {
            if (!Uri.IsHexDigit(character))
            {
                throw new ArgumentException(
                    "SHA-256 인증서 지문은 16진수여야 합니다.");
            }
        }

        return (_, certificate, _, _) =>
            certificate is not null &&
            string.Equals(
                certificate.GetCertHashString(HashAlgorithmName.SHA256),
                expectedFingerprint,
                StringComparison.OrdinalIgnoreCase);
    }
}
