package org.jlab.coda.cMsg.remoteExec;

import org.jlab.coda.cMsg.common.Base64;

import javax.crypto.*;
import javax.crypto.spec.DESKeySpec;
import javax.crypto.spec.SecretKeySpec;
import java.io.UnsupportedEncodingException;
import java.security.NoSuchAlgorithmException;
import java.security.InvalidKeyException;


/**
 * This class is used to encrypt passwords which in turn allow
 * users to retrict the ability of Commanders to work with
 * password-enbled Executors. Can use either DES (64 bit) or
 * AES (128 bit) encryption.
 * Singleton.
 *
 * @author timmer
 * Date: Nov 1, 2010
 */
public class ExecutorSecurity {

    static Cipher encipherDES = null;
    static Cipher decipherDES = null;

    static Cipher encipherAES = null;
    static Cipher decipherAES = null;

    static {
        try {
            encipherDES = Cipher.getInstance("DES");
            decipherDES = Cipher.getInstance("DES");

            encipherAES = Cipher.getInstance("AES");
            decipherAES = Cipher.getInstance("AES");

            // Previously generated keys, in byte array form.
            byte[] keyDataDES = {(byte)-94, (byte)-97, (byte)37,  (byte)-91,
                                 (byte)30,  (byte)-37, (byte)123, (byte)-13,
                                 (byte)-82, (byte)93 , (byte)-2,  (byte)20,
                                 (byte)-49, (byte)22,  (byte)119, (byte)36};

            byte[] keyDataAES = {(byte)-121, (byte)-59,  (byte)-26, (byte)12,
                                 (byte)-51,  (byte)-29,  (byte)-26, (byte)86,
                                 (byte)110,  (byte)25 ,  (byte)-23, (byte)-27,
                                 (byte)112,  (byte)-80,  (byte)77,  (byte)102 };

            // Create SecretKey from DES keyData
            DESKeySpec desKeySpec = new DESKeySpec(keyDataDES);
            SecretKeyFactory keyFactory = SecretKeyFactory.getInstance("DES");
            SecretKey secretKey = keyFactory.generateSecret(desKeySpec);

            // Create SecretKey from AES keyData
            SecretKeySpec skeySpec = new SecretKeySpec(keyDataAES, "AES");

            // Create objects to encode/decode strings
            encipherDES.init(Cipher.ENCRYPT_MODE, secretKey);
            decipherDES.init(Cipher.DECRYPT_MODE, secretKey);

            encipherAES.init(Cipher.ENCRYPT_MODE, skeySpec);
            decipherAES.init(Cipher.DECRYPT_MODE, skeySpec);
        }
        catch (java.security.spec.InvalidKeySpecException e) {
            e.printStackTrace();
        }
        catch (javax.crypto.NoSuchPaddingException e) {
            e.printStackTrace();
        }
        catch (java.security.NoSuchAlgorithmException e) {
            e.printStackTrace();
        }
        catch (java.security.InvalidKeyException e) {
            e.printStackTrace();
        }
    }


    /**
     * Use this method to generate a new AES key in byte array form.
     * This array used in the static block when it is turned back into a key.
     * @return secret key in byte array form
     */
    static public byte[] generateKeyDataAES() {

        try {
            KeyGenerator kgen = KeyGenerator.getInstance("AES");
            kgen.init(128); // 192 and 256 bits may not be available
            SecretKey key = kgen.generateKey();
            return key.getEncoded();
        }
        catch (NoSuchAlgorithmException e) {}

        return null;
    }

    /**
     * Use this method to generate a new DES key in byte array form.
     * This array used in the static block when it is turned back into a key.
     * @return secret key in byte array form
     */
    static public byte[] generateKeyDataDES() {

        try {
            SecretKey key = KeyGenerator.getInstance("DES").generateKey();
            Cipher wrapCipher = Cipher.getInstance("DES");
            wrapCipher.init(Cipher.WRAP_MODE, key);
            return wrapCipher.wrap(key);
        }
        catch (NoSuchAlgorithmException e) {}
        catch (NoSuchPaddingException e) {}
        catch (InvalidKeyException e) {}
        catch (IllegalBlockSizeException e) {}

        return null;
    }

    /**
     * Encrypt string.
     * @param str string to encrypt
     * @return encrypted string.
     */
    static public String encrypt(String str) {
        try {
            // Encode the string into bytes using utf-8
            byte[] utf8 = str.getBytes("UTF8");

            // Encrypt (change AES to DES or vice versa for different encryption algorithm)
            byte[] enc = encipherAES.doFinal(utf8);

            // Encode bytes to base64 to get a string
            return Base64.encodeToString(enc, false);
        }
        catch (javax.crypto.BadPaddingException e) { }
        catch (IllegalBlockSizeException e) { }
        catch (UnsupportedEncodingException e) {/*never happen*/}
        catch (java.io.IOException e) { }

        return null;
    }

    /**
     * Decrypt encrypted string.
     * @param str encrypted string.
     * @return decrypted string.
     */
    static public String decrypt(String str) {
        try {
            // Decode base64 to get bytes
            byte[] dec = Base64.decodeToBytes(str, "UTF8");

            // Decrypt (change AES to DES or vice versa for different decryption algorithm)
            byte[] utf8 = decipherAES.doFinal(dec);

            // Decode using utf-8
            return new String(utf8, "UTF8");
        }
        catch (javax.crypto.BadPaddingException e) { }
        catch (IllegalBlockSizeException e) { }
        catch (UnsupportedEncodingException e) {/*never happen*/}
        catch (java.io.IOException e) { }

        return null;
    }

    
    /**
     * Here's an example that uses this class to generate a key and
     * use that to encrypt and decrypt a string.
     * @param args
     */
    public static void main(String[] args) {
        try {
            byte[] keyBytes = ExecutorSecurity.generateKeyDataAES();
            for (int i=0; i<keyBytes.length; i++) {
                System.out.println("byte[" + i + "] = " + keyBytes[i]);
            }

            // Encrypt
            System.out.println("Encrypt the string \"Don't tell anyboday!\"");
            String encrypted = ExecutorSecurity.encrypt("Don't tell anybody!");

            // Decrypt
            String decrypted = ExecutorSecurity.decrypt(encrypted);
            System.out.println("Decrypted string = " + decrypted);
       }
        catch (Exception e) {
            e.printStackTrace();
        }
    }


}
