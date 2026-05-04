package com.syncflow

import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import com.syncflow.databinding.ActivitySettingsBinding
import org.json.JSONObject
import java.io.File

class SettingsActivity : AppCompatActivity() {
    private lateinit var binding: ActivitySettingsBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivitySettingsBinding.inflate(layoutInflater)
        setContentView(binding.root)

        val cfgFile = File(filesDir, "config.json")
        if (cfgFile.exists()) {
            try {
                val text = cfgFile.readText()
                val root = JSONObject(text)
                val fs = root.optJSONObject("file_sync") ?: JSONObject()
                val sec = root.optJSONObject("security") ?: JSONObject()

                binding.etDeviceName.setText(fs.optString("device_name", ""))
                binding.etSourcePath.setText(fs.optString("source_path", "sync/"))
                binding.etReceiveDir.setText(fs.optString("receive_dir", "received"))
                binding.cbFileSync.isChecked = fs.optBoolean("enabled", true)
                binding.cbSecurity.isChecked = sec.optBoolean("enabled", true)
                binding.cbRequireApproval.isChecked = sec.optBoolean("require_approval", true)
            } catch (e: Exception) {
                // ignore and use defaults
            }
        }

        binding.btnSave.setOnClickListener {
            val root = JSONObject()
            val fs = JSONObject()
            val sec = JSONObject()

            fs.put("enabled", binding.cbFileSync.isChecked)
            fs.put("source_path", binding.etSourcePath.text.toString())
            fs.put("receive_dir", binding.etReceiveDir.text.toString())
            fs.put("device_name", binding.etDeviceName.text.toString())

            sec.put("enabled", binding.cbSecurity.isChecked)
            sec.put("require_approval", binding.cbRequireApproval.isChecked)

            root.put("file_sync", fs)
            root.put("security", sec)

            cfgFile.writeText(root.toString(2))
            finish()
        }

        binding.btnCancel.setOnClickListener {
            finish()
        }
    }
}
