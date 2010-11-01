package org.jlab.coda.cMsg.remoteExec;

import javax.crypto.*;
import javax.crypto.spec.DESKeySpec;
import java.io.UnsupportedEncodingException;
import java.security.NoSuchAlgorithmException;
import java.security.InvalidKeyException;


/**
 * This class is used to encrypt passwords which in turn allow
 * users to retrict the ability of Commanders to work with
 * password-enbled Executors. Singleton.
 *
 * @author timmer
 * Date: Nov 1, 2010
 */
public class ExecutorSecurity {

    static Cipher encipher = null;
    static Cipher decipher = null;

    static {
        try {
            encipher = Cipher.getInstance("DES");
            decipher = Cipher.getInstance("DES");

            // Previously generated key, wrapped in byte array
            byte[] keyData  = {(byte)-94, (byte)-97, (byte)37,  (byte)-91,
                               (byte)30,  (byte)-37, (byte)123, (byte)-13,
                               (byte)-82, (byte)93 , (byte)-2,  (byte)20,
                               (byte)-49, (byte)22,  (byte)119, (byte)36};

            // Create SecretKey from keyData
            DESKeySpec desKeySpec = new DESKeySpec(keyData);
            SecretKeyFactory keyFactory = SecretKeyFactory.getInstance("DES");
            SecretKey secretKey = keyFactory.generateSecret(desKeySpec);

            // Create objects to encode/decode strings
            encipher.init(Cipher.ENCRYPT_MODE, secretKey);
            decipher.init(Cipher.DECRYPT_MODE, secretKey);
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
     * Use this method to generate a new key in byte array form.
     * This array used in the static block when it is turned back into a key.
     * @return secret key in byte array form
     */
    static public byte[] generateKeyData() {

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
            // Encrypt
            byte[] enc = encipher.doFinal(utf8);
            // Encode bytes to base64 to get a string
            return new sun.misc.BASE64Encoder().encode(enc);
        }
        catch (javax.crypto.BadPaddingException e) { }
        catch (IllegalBlockSizeException e) { }
        catch (UnsupportedEncodingException e) { }
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
            byte[] dec = new sun.misc.BASE64Decoder().decodeBuffer(str);
            // Decrypt
            byte[] utf8 = decipher.doFinal(dec);
            // Decode using utf-8
            return new String(utf8, "UTF8");
        }
        catch (javax.crypto.BadPaddingException e) { }
        catch (IllegalBlockSizeException e) { }
        catch (UnsupportedEncodingException e) { }
        catch (java.io.IOException e) { }

        return null;
    }

    // Here's an example that uses the class:
    public static void main(String[] args) {
        try {
            byte[] keyBytes = ExecutorSecurity.generateKeyData();
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
