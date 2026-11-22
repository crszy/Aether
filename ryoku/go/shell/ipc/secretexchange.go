package main

import (
	"bytes"
	"crypto/aes"
	"crypto/cipher"
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"encoding/base64"
	"errors"
	"math/big"
	"strings"
)

// secretExchange is the keyring "sx-aes-1" secret exchange (gcr's
// GcrSecretExchange), reimplemented so the daemon can act as the GNOME keyring
// system prompter and exchange the typed password with gnome-keyring-daemon
// without it ever crossing the bus in the clear.
//
// It must be byte-for-byte interoperable with gcr (egg-dh-libgcrypt.c +
// gcr-secret-exchange.c), so the wire choices below are not free:
//   - key agreement is Diffie-Hellman over the 1536-bit IKE MODP group
//     (RFC 3526 group 5), generator 2;
//   - a public key is encoded minimal big-endian, but the shared secret is
//     left-zero-padded to the prime length (192 bytes) before key derivation;
//   - the transport key is HKDF-SHA256 (empty salt -> 32 zero bytes, empty
//     info) truncated to 16 bytes;
//   - the secret is AES-128-CBC with PKCS#7 padding under a random 16-byte IV.
//
// The wire form is a GLib key file: a "[sx-aes-1]" group with base64 "public",
// and (when a secret is sent) "secret" and "iv" values.
const sxProtocol = "sx-aes-1"

// sxPrimeLen is the byte length of the group prime; the shared secret is padded
// to it, matching gcr so both sides derive the same key material.
const sxPrimeLen = 192

var (
	// The 1536-bit MODP group prime (RFC 3526). Identical to gcr's
	// dh_group_1536_prime; the bit length is asserted in tests.
	sxPrime, _ = new(big.Int).SetString(""+
		"FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD1"+
		"29024E088A67CC74020BBEA63B139B22514A08798E3404DD"+
		"EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245"+
		"E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED"+
		"EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3D"+
		"C2007CB8A163BF0598DA48361C55D39A69163FA8FD24CF5F"+
		"83655D23DCA3AD961C62F356208552BB9ED529077096966D"+
		"670C354E4ABC9804F1746C08CA237327FFFFFFFFFFFFFFFF", 16)
	sxGen = big.NewInt(2)
)

type secretExchange struct {
	priv    *big.Int
	pub     *big.Int
	key     []byte // 16-byte AES key, valid once derived
	derived bool
}

func newSecretExchange() *secretExchange { return &secretExchange{} }

// generate creates the DH key pair once. Any valid exponent interoperates, so a
// full-range secret is used rather than gcr's bit-trimmed one.
func (e *secretExchange) generate() error {
	if e.priv != nil {
		return nil
	}
	max := new(big.Int).Sub(sxPrime, big.NewInt(2)) // [0, p-3)
	k, err := rand.Int(rand.Reader, max)
	if err != nil {
		return err
	}
	k.Add(k, big.NewInt(1)) // -> [1, p-2]
	e.priv = k
	e.pub = new(big.Int).Exp(sxGen, k, sxPrime)
	return nil
}

// begin starts the exchange and returns the wire string carrying our public key
// (no secret). Mirrors gcr_secret_exchange_begin.
func (e *secretExchange) begin() (string, error) {
	if err := e.generate(); err != nil {
		return "", err
	}
	return encodeKeyFile([]kfEntry{{"public", b64(e.pub.Bytes())}}), nil
}

// receive parses a wire string from the peer: it derives the shared transport
// key from their public key (generating ours first if needed) and decrypts a
// secret when one is present. Mirrors gcr_secret_exchange_receive.
func (e *secretExchange) receive(s string) ([]byte, error) {
	fields := parseKeyFile(s)
	if err := e.generate(); err != nil {
		return nil, err
	}
	if !e.derived {
		peerB64, ok := fields["public"]
		if !ok || peerB64 == "" {
			return nil, errors.New("secret exchange: missing public key")
		}
		peerBytes, err := base64.StdEncoding.DecodeString(peerB64)
		if err != nil {
			return nil, err
		}
		peer := new(big.Int).SetBytes(peerBytes)
		shared := new(big.Int).Exp(peer, e.priv, sxPrime)
		ikm := make([]byte, sxPrimeLen)
		shared.FillBytes(ikm) // left zero-padded to the prime length, like gcr
		e.key = hkdfSHA256(ikm, nil, nil, 16)
		e.derived = true
	}
	secB64, ok := fields["secret"]
	if !ok {
		return nil, nil
	}
	iv, err := base64.StdEncoding.DecodeString(fields["iv"])
	if err != nil {
		return nil, err
	}
	ct, err := base64.StdEncoding.DecodeString(secB64)
	if err != nil {
		return nil, err
	}
	return e.decrypt(ct, iv)
}

// send builds a wire string carrying our public key and, when secret is non-nil,
// the encrypted secret and its IV. receive must have run first. Mirrors
// gcr_secret_exchange_send. A nil secret sends a reply with no secret; an empty
// (non-nil) secret encrypts the empty password.
func (e *secretExchange) send(secret []byte) (string, error) {
	if !e.derived {
		return "", errors.New("secret exchange: key not yet derived")
	}
	entries := []kfEntry{{"public", b64(e.pub.Bytes())}}
	if secret != nil {
		ct, iv, err := e.encrypt(secret)
		if err != nil {
			return "", err
		}
		entries = append(entries, kfEntry{"secret", b64(ct)}, kfEntry{"iv", b64(iv)})
	}
	return encodeKeyFile(entries), nil
}

func (e *secretExchange) encrypt(plain []byte) (ct, iv []byte, err error) {
	block, err := aes.NewCipher(e.key)
	if err != nil {
		return nil, nil, err
	}
	iv = make([]byte, 16)
	if _, err = rand.Read(iv); err != nil {
		return nil, nil, err
	}
	padded := pkcs7Pad(plain, 16)
	ct = make([]byte, len(padded))
	cipher.NewCBCEncrypter(block, iv).CryptBlocks(ct, padded)
	return ct, iv, nil
}

func (e *secretExchange) decrypt(ct, iv []byte) ([]byte, error) {
	if len(iv) != 16 {
		return nil, errors.New("secret exchange: bad iv length")
	}
	if len(ct) == 0 || len(ct)%16 != 0 {
		return nil, errors.New("secret exchange: bad ciphertext length")
	}
	block, err := aes.NewCipher(e.key)
	if err != nil {
		return nil, err
	}
	pt := make([]byte, len(ct))
	cipher.NewCBCDecrypter(block, iv).CryptBlocks(pt, ct)
	return pkcs7Unpad(pt, 16)
}

// hkdfSHA256 is RFC 5869 HKDF with SHA-256, matching egg_hkdf_perform: a nil
// salt defaults to HashLen zero bytes and a nil info contributes nothing.
func hkdfSHA256(ikm, salt, info []byte, length int) []byte {
	if salt == nil {
		salt = make([]byte, sha256.Size)
	}
	extract := hmac.New(sha256.New, salt)
	extract.Write(ikm)
	prk := extract.Sum(nil)

	out := make([]byte, 0, length)
	var prev []byte
	for i := byte(1); len(out) < length; i++ {
		h := hmac.New(sha256.New, prk)
		h.Write(prev)
		h.Write(info)
		h.Write([]byte{i})
		prev = h.Sum(nil)
		out = append(out, prev...)
	}
	return out[:length]
}

// pkcs7Pad appends 1..block bytes (a full block when already aligned), matching
// egg_padding_pkcs7_pad.
func pkcs7Pad(data []byte, block int) []byte {
	n := block - len(data)%block
	return append(append([]byte{}, data...), bytes.Repeat([]byte{byte(n)}, n)...)
}

func pkcs7Unpad(data []byte, block int) ([]byte, error) {
	n := len(data)
	if n == 0 || n%block != 0 {
		return nil, errors.New("secret exchange: bad padding")
	}
	pad := int(data[n-1])
	if pad == 0 || pad > block || pad > n {
		return nil, errors.New("secret exchange: bad padding")
	}
	for i := n - pad; i < n; i++ {
		if data[i] != byte(pad) {
			return nil, errors.New("secret exchange: bad padding")
		}
	}
	return data[:n-pad], nil
}

type kfEntry struct{ key, val string }

// encodeKeyFile writes the minimal GLib key-file form gcr produces: the group
// header then "key=value" lines.
func encodeKeyFile(entries []kfEntry) string {
	var b strings.Builder
	b.WriteString("[" + sxProtocol + "]\n")
	for _, e := range entries {
		b.WriteString(e.key + "=" + e.val + "\n")
	}
	return b.String()
}

// parseKeyFile reads the values out of a gcr exchange string. The exchange only
// ever carries one group, so group headers are ignored and every "key=value"
// line is collected.
func parseKeyFile(s string) map[string]string {
	out := map[string]string{}
	for _, line := range strings.Split(s, "\n") {
		line = strings.TrimSpace(line)
		if line == "" || strings.HasPrefix(line, "[") || strings.HasPrefix(line, "#") {
			continue
		}
		if i := strings.IndexByte(line, '='); i > 0 {
			out[strings.TrimSpace(line[:i])] = strings.TrimSpace(line[i+1:])
		}
	}
	return out
}

func b64(b []byte) string { return base64.StdEncoding.EncodeToString(b) }
