package com.syncflow

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.widget.Button
import android.widget.EditText
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import org.json.JSONObject
import java.io.File

class SettingsActivity : AppCompatActivity() {
    private lateinit var deviceNameEdit: EditText
    private lateinit var sourcePathEdit: EditText
    private lateinit var receiveDirEdit: EditText
    private lateinit var saveButton: Button

    private lateinit var pickDirLauncher: ActivityResultLauncher<Uri?>

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_settings)

        deviceNameEdit = findViewById(R.id.deviceNameEdit)
        sourcePathEdit = findViewById(R.id.sourcePathEdit)
        receiveDirEdit = findViewById(R.id.receiveDirEdit)
        saveButton = findViewById(R.id.saveButton)

        pickDirLauncher = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri: Uri? ->
            if (uri != null) {
                try {
                    val takeFlags = Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                    contentResolver.takePersistableUriPermission(uri, takeFlags)
                } catch (e: Exception) {
                    // ignore permission errors
                }
                receiveDirEdit.setText(uri.toString())
            }
        }

        // make receive dir field launch the SAF picker when clicked
        receiveDirEdit.isFocusable = false
        receiveDirEdit.setOnClickListener { pickDirLauncher.launch(null) }

        loadConfig()

        saveButton.setOnClickListener {
            saveConfig()
            finish()
        }
    }

    private fun configFile(): File = filesDir.resolve("config.json")

    private fun loadConfig() {
        val f = configFile()
        if (!f.exists()) return
        val obj = JSONObject(f.readText())
        val fs = obj.optJSONObject("file_sync")
        deviceNameEdit.setText(fs?.optString("device_name", ""))
        sourcePathEdit.setText(fs?.optString("source_path", "sync/"))
        receiveDirEdit.setText(fs?.optString("receive_dir", "received"))
    }

    private fun saveConfig() {
        val f = configFile()
        val existing = if (f.exists()) JSONObject(f.readText()) else JSONObject()

        val fs = JSONObject()
        fs.put("enabled", true)
        fs.put("source_path", sourcePathEdit.text.toString())
        fs.put("receive_dir", receiveDirEdit.text.toString())
        fs.put("device_name", deviceNameEdit.text.toString())

        existing.put("file_sync", fs)
        // preserve security section if present
        if (!existing.has("security")) {
            val sec = JSONObject()
            sec.put("enabled", true)
            sec.put("require_approval", true)
            existing.put("security", sec)
        }

        f.writeText(existing.toString(2))
    }
}
