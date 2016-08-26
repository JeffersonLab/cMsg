/*---------------------------------------------------------------------------*
*  Copyright (c) 2010        Jefferson Science Associates,                   *
*                            Thomas Jefferson National Accelerator Facility  *
*                                                                            *
*    This software was developed under a United States Government license    *
*    described in the NOTICE file included as part of this distribution.     *
*                                                                            *
*    C.Timmer, 22-Nov-2010, Jefferson Lab                                    *
*                                                                            *
*    Authors: Carl Timmer                                                    *
*             timmer@jlab.org                   Jefferson Lab, #10           *
*             Phone: (757) 269-5130             12000 Jefferson Ave.         *
*             Fax:   (757) 269-6248             Newport News, VA 23606       *
*                                                                            *
*----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.remoteExec;

import javax.crypto.*;
import javax.crypto.spec.SecretKeySpec;
import java.io.UnsupportedEncodingException;
import java.security.NoSuchAlgorithmException;
import java.text.SimpleDateFormat;
import java.util.Base64;


/**
 * This class is used to encrypt passwords which in turn allow
 * users to restrict the ability of Commanders to work with
 * password-enabled Executors. Uses AES (128 bit key) encryption.
 * Singleton.
 *
 * @author timmer
 * Date: Nov 1, 2010
 */
public class ExecutorSecurity {

    static Cipher encipherAES = null;
    static Cipher decipherAES = null;

    static String cipherType    = "AES";  // is equivalent to "AES/ECB/PKCS5Padding"
    static String secretKeyType = "AES";

    /** Encoder for binary data. */
    static final Base64.Encoder b64Encoder;

    /** Decoder for binary data. */
    static final Base64.Decoder b64Decoder;


    static {

        b64Encoder = Base64.getEncoder();
        b64Decoder = Base64.getDecoder();

        try {

            encipherAES = Cipher.getInstance(cipherType);
            decipherAES = Cipher.getInstance(cipherType);

            byte[] keyDataAES = {(byte)-121, (byte)-59,  (byte)-26, (byte)12,
                                 (byte)-51,  (byte)-29,  (byte)-26, (byte)86,
                                 (byte)110,  (byte)25 ,  (byte)-23, (byte)-27,
                                 (byte)112,  (byte)-80,  (byte)77,  (byte)102 };

            // Create SecretKey from AES keyData
            SecretKeySpec skeySpec = new SecretKeySpec(keyDataAES, secretKeyType);

            // Create objects to encode/decode strings
            encipherAES.init(Cipher.ENCRYPT_MODE, skeySpec);
            decipherAES.init(Cipher.DECRYPT_MODE, skeySpec);
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
            KeyGenerator kgen = KeyGenerator.getInstance(cipherType);
            kgen.init(128); // 192 and 256 bits may not be available
            SecretKey key = kgen.generateKey();
            return key.getEncoded();
        }
        catch (NoSuchAlgorithmException e) {}

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
            byte[] enc = encipherAES.doFinal(utf8);

            // Encode bytes to base64 to get a string
            return b64Encoder.encodeToString(enc);
        }
        catch (javax.crypto.BadPaddingException e) { }
        catch (IllegalBlockSizeException e) { }
        catch (UnsupportedEncodingException e) {/*never happen*/}

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
            byte[] dec = b64Decoder.decode(str);

            // Decrypt
            byte[] utf8 = decipherAES.doFinal(dec);

            // Decode using utf-8
            return new String(utf8, "UTF8");
        }
        catch (javax.crypto.BadPaddingException e) { }
        catch (IllegalBlockSizeException e) { }
        catch (UnsupportedEncodingException e) {/*never happen*/}

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
